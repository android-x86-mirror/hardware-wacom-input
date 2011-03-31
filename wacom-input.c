/*****************************************************************************
** wacom-input.c
**
** Copyright (C) 2011 Stefan Seidel
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
**
**
** Code inspired by wacdump.c from http://linuxwacom.sourceforge.net and
** uniput-sample.c from http://thiemonge.org/getting-started-with-uinput
**
**
** Version history:
**    0.1 - 2011-03-29 - initial support for "tpc" device
**
****************************************************************************/

#include "wactablet.h"
#include "wacserial.h"
#include "wacusb.h"

#include <ctype.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <linux/input.h>
#include <linux/uinput.h>

#define die(str, args...) do { \
        perror(str, ## args); \
        exit(EXIT_FAILURE); \
    } while(0)

#define WACOM_MIN_PRESSURE 40

int fd;

void wacom_report_event(__u16 type, __u16 code, __s32 value) {
    struct input_event ev;
    if (value == -1) {
	return;
    }
    memset(&ev, 0, sizeof(struct input_event));
    ev.type = type;
    ev.code = code;
    ev.value = value;
    if(write(fd, &ev, sizeof(struct input_event)) < 0)
        perror("error: write");
}


static void signal_handler(int signo) {
    if(ioctl(fd, UI_DEV_DESTROY) < 0) {
	die("error: cannot destroy uinput device\n");
    }
    close(fd);
    exit(EXIT_SUCCESS);
}

int main(void) {
    struct uinput_user_dev uidev;
    WACOMENGINE hEngine = NULL;
    WACOMTABLET hTablet = NULL;
    WACOMSTATE state = WACOMSTATE_INIT;
    WACOMMODEL model = { 0 };
    unsigned char uchBuf[64];
    int nLength = 0;

    if(signal(SIGINT,signal_handler)==SIG_ERR) {
	die("error registering signal handler\n");
    }

    // set up uinput device
    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if(fd < 0) {
	fd = open("/dev/input/uinput", O_WRONLY | O_NONBLOCK);
    }

    if(fd < 0)
        die("error: opening /dev/[input/]uinput failed");

    // report that we have TOUCH events ...
    if(ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0)
        die("error: ioctl");
    if(ioctl(fd, UI_SET_KEYBIT, BTN_TOUCH) < 0)
        die("error: ioctl");

    // and absolute x, y, pressure data
    if(ioctl(fd, UI_SET_EVBIT, EV_ABS) < 0)
        die("error: ioctl");
    if(ioctl(fd, UI_SET_ABSBIT, ABS_X) < 0)
        die("error: ioctl");
    if(ioctl(fd, UI_SET_ABSBIT, ABS_Y) < 0)
        die("error: ioctl");
    if(ioctl(fd, UI_SET_ABSBIT, ABS_PRESSURE) < 0)
        die("error: ioctl");

    // this is for simulated mouse middle/right button
//    if(ioctl(fd, UI_SET_KEYBIT, BTN_MOUSE) < 0)
//        die("error: ioctl");
//    if(ioctl(fd, UI_SET_KEYBIT, BTN_RIGHT) < 0)
//        die("error: ioctl");
//    if(ioctl(fd, UI_SET_KEYBIT, BTN_LEFT) < 0)
//        die("error: ioctl");

    // this could be more detailed, but in the end, who cares?
    memset(&uidev, 0, sizeof(uidev));
    snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "wacom-input");
    uidev.absmin[ABS_X] = 0;
    uidev.absmax[ABS_X] = 24576;
    uidev.absmin[ABS_Y] = 0;
    uidev.absmax[ABS_Y] = 18432;
    uidev.absmin[ABS_PRESSURE] = 0;
    uidev.absmax[ABS_PRESSURE] = 255;
    uidev.id.bustype = BUS_RS232;
    uidev.id.vendor  = 0x056a;
    uidev.id.product = 0xffff;
    uidev.id.version = 1;

    if(write(fd, &uidev, sizeof(uidev)) < 0)
        die("error: set virtual device info 1");

    if(ioctl(fd, UI_DEV_CREATE) < 0)
        die("error: create uinput device 1");
    // uinput is set up

    // connect to wacom device
    hEngine = WacomInitEngine();
    if (!hEngine) {
	die("failed to open tablet engine");
    }

    /* open tablet */
    model.uClass = WACOMCLASS_SERIAL;
    model.uDevice =  WacomGetDeviceFromName("tpc",model.uClass);
    hTablet = WacomOpenTablet(hEngine,"/dev/ttyS0",&model);
    if (!hTablet) {
	die ("WacomOpenTablet");
    }
    // wacom device is set up properly

    while(1) {
	if ((nLength = WacomReadRaw(hTablet,uchBuf,sizeof(uchBuf))) < 0) {
	    continue;
	}
	if (WacomParseData(hTablet,uchBuf,nLength,&state)) {
	    continue;
	}
	if (!state.values[WACOMFIELD_PROXIMITY].nValue) {
	    // no tool in proximity
	    wacom_report_event(EV_ABS, ABS_PRESSURE, 0);
	    wacom_report_event(EV_KEY, BTN_TOUCH,    0);
//	    wacom_report_event(EV_KEY, BTN_RIGHT,    0);
//	    wacom_report_event(EV_KEY, BTN_MIDDLE,   0);
	    wacom_report_event(EV_SYN, SYN_REPORT,   0);
	    continue;
	}

        wacom_report_event(EV_ABS, ABS_X,        state.values[WACOMFIELD_POSITION_X].nValue);
        wacom_report_event(EV_ABS, ABS_Y,        state.values[WACOMFIELD_POSITION_Y].nValue);
        wacom_report_event(EV_ABS, ABS_PRESSURE, state.values[WACOMFIELD_PRESSURE].nValue);
        wacom_report_event(EV_KEY, BTN_TOUCH,    state.values[WACOMFIELD_PRESSURE].nValue > WACOM_MIN_PRESSURE);
//        wacom_report_event(EV_KEY, BTN_RIGHT,    state.values[WACOMFIELD_BUTTONS].nValue == WACOMBUTTON_STYLUS);
//        wacom_report_event(EV_KEY, BTN_MIDDLE,   state.values[WACOMFIELD_TOOLTYPE].nValue == WACOMTOOLTYPE_ERASER);
        wacom_report_event(EV_SYN, SYN_REPORT,   0);
    }


    if(ioctl(fd, UI_DEV_DESTROY) < 0)
        die("error: ioctl");

    close(fd);

    return 0;
}
