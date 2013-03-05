/*
 * This file is part of the OpenKinect Project. http://www.openkinect.org
 *
 * Copyright (c) 2010 individual OpenKinect contributors. See the CONTRIB file
 * for details.
 *
 * This code is licensed to you under the terms of the Apache License, version
 * 2.0, or, at your option, the terms of the GNU General Public License,
 * version 2.0. See the APACHE20 and GPL2 files for the text of the licenses,
 * or the following URLs:
 * http://www.apache.org/licenses/LICENSE-2.0
 * http://www.gnu.org/licenses/gpl-2.0.txt
 *
 * If you redistribute this file in source form, modified or unmodified, you
 * may:
 *   1) Leave this header intact and distribute it under the same terms,
 *      accompanying it with the APACHE20 and GPL20 files, or
 *   2) Delete the Apache 2.0 clause and accompany it with the GPL2 file, or
 *   3) Delete the GPL v2 clause and accompany it with the APACHE20 file
 * In all cases you must keep the copyright notice intact and include a copy
 * of the CONTRIB file.
 *
 * Binary distributions must follow the binary distribution requirements of
 * either License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include "freenect_internal.h"

// The kinect can tilt from +31 to -31 degrees in what looks like 1 degree increments
// The control input looks like 2*desired_degrees
#define MAX_TILT_ANGLE 31
#define MIN_TILT_ANGLE (-31)

#define GRAVITY 9.80665

uint32_t tag_seq = 1;
uint32_t tag_next_ack = 1;

typedef struct {
	uint32_t magic;
	uint32_t tag;
	uint32_t arg1;
	uint32_t cmd;
	uint32_t arg2;
} motor_command;

typedef struct {
	uint32_t magic;
	uint32_t tag;
	uint32_t status;
} motor_reply;

int freenect_tilt_get_reply(fnusb_dev *dev, freenect_context *ctx) {
	unsigned char buffer[1024];
	memset(buffer, 0, 1024);
	int transferred = 0;
	int res = 0;

	res = fnusb_bulk(dev, 0x81, buffer, 1024, &transferred);
	if (res != 0) {
		FN_ERROR("freenect_tilt_get_reply(): bulk_transfer failed: %d (transferred = %d)\n", res, transferred);
	} else if (transferred != 12) {
		FN_ERROR("freenect_tilt_get_reply(): weird - got %d bytes (expected 12)\n", transferred);
	} else {
		motor_reply reply;
		memcpy(&reply, buffer, sizeof(reply));
		if (reply.magic != 0x0a6fe000) {
			FN_ERROR("Bad magic: %08X (expected 0A6FE000\n", reply.magic);
			res = -1;
		}
		if (reply.tag != tag_next_ack) {
			FN_ERROR("Reply tag out of order: expected %d, got %d\n", tag_next_ack, reply.tag);
			res = -1;
		}
		if (reply.status != 0) {
			FN_ERROR("reply status != 0: failure?\n");
			res = -1;
		}
		tag_next_ack++;
	}
	return res;
}

freenect_raw_tilt_state* freenect_get_tilt_state(freenect_device *dev)
{
	return &dev->raw_state;
}

int freenect_update_tilt_state(freenect_device *dev)
{
	freenect_context *ctx = dev->parent;
	uint8_t buf[10];
	uint16_t ux, uy, uz;
	int ret = 0;
		
	if (dev->usb_motor.dev) {
		if (dev->model == KINECT_MODEL_K4W) {
			int transferred = 0;
			int res = 0;
			motor_command cmd;
			cmd.magic = fn_le32(0x06022009);
			cmd.tag = fn_le32(tag_seq++);
			cmd.arg1 = fn_le32(0x68); // 104.  Incidentally, the number of bytes that we expect in the reply.
			cmd.cmd = fn_le32(0x8032);
			unsigned char buffer[256];
			memcpy(buffer, &cmd, 16);
			res = fnusb_bulk(&dev->usb_motor, 0x01, buffer, 16, &transferred);
			if (res != 0) {
				FN_ERROR("update_tilt_state(): bulk_transfer failed: %d (transferred = %d)\n", res, transferred);
				return res;
			}
			res = fnusb_bulk(&dev->usb_motor, 0x81, buffer, 256, &transferred); // 104 bytes
			if (res != 0) {
				FN_ERROR("update_tilt_state(): bulk_transfer failed: %d (transferred = %d)\n", res, transferred);
				return res;
			} else {
				int i;
				for(i = 0 ; i < transferred ; i += 4) {
					int32_t j;
					memcpy(&j, buffer + i, 4);
				}
				struct {
					int32_t x;
					int32_t y;
					int32_t z;
					int32_t tilt;
					int32_t status;
				} accel;
				memcpy(&accel, buffer + 16, sizeof(accel));
				dev->raw_state.accelerometer_x = (int16_t)accel.x;
				dev->raw_state.accelerometer_y = (int16_t)accel.y;
				dev->raw_state.accelerometer_z = (int16_t)accel.z;
				dev->raw_state.tilt_angle = (int8_t)accel.tilt * 2;
				dev->raw_state.tilt_status = (freenect_tilt_status_code)accel.status;
			}

			ret = freenect_tilt_get_reply(&dev->usb_motor, ctx);

		} if (dev->model == KINECT_MODEL_1414) {
			ret = fnusb_control(&dev->usb_motor, 0xC0, 0x32, 0x0, 0x0, buf, 10);
			if (ret != 10) {
				FN_ERROR("Error in accelerometer reading, libusb_control_transfer returned %d\n", ret);
				return ret < 0 ? ret : -1;
			}
			
			ux = ((uint16_t)buf[2] << 8) | buf[3];
			uy = ((uint16_t)buf[4] << 8) | buf[5];
			uz = ((uint16_t)buf[6] << 8) | buf[7];
			
			dev->raw_state.accelerometer_x = (int16_t)ux;
			dev->raw_state.accelerometer_y = (int16_t)uy;
			dev->raw_state.accelerometer_z = (int16_t)uz;
			dev->raw_state.tilt_angle = (int8_t)buf[8];
			dev->raw_state.tilt_status = (freenect_tilt_status_code)buf[9];
		}
	}

	return ret;
}

int freenect_set_tilt_degs(freenect_device *dev, double angle)
{
	int ret = 0;
	freenect_context *ctx = dev->parent;

	if (dev->usb_motor.dev) {
		if (dev->model == KINECT_MODEL_K4W) {
			int32_t tilt_degrees = (int32_t) angle;
			angle = (angle<MIN_TILT_ANGLE) ? MIN_TILT_ANGLE : ((angle>MAX_TILT_ANGLE) ? MAX_TILT_ANGLE : angle);
			angle = angle * 2;
			
			if (tilt_degrees > 31 || tilt_degrees < -31) {
				FN_ERROR("set_tilt(): degrees %d out of safe range [-31, 31]\n", tilt_degrees);
				return -1;
			}
			motor_command cmd;
			cmd.magic = fn_le32(0x06022009);
			cmd.tag = fn_le32(tag_seq++);
			cmd.arg1 = fn_le32(0);
			cmd.cmd = fn_le32(0x803B);
			cmd.arg2 = fn_le32((int32_t)tilt_degrees);
			int transferred = 0;
			int res = 0;
			unsigned char buffer[56];
			memset(buffer, 0, 56);
			memcpy(buffer, &cmd, sizeof(motor_command));
			res = fnusb_bulk(&dev->usb_motor, 0x01, buffer, 56, &transferred);
			if (res != 0) {
				FN_ERROR("set_tilt(): bulk_transfer failed: %d (transferred = %d)\n", res, transferred);
				return res;
			}
			ret = freenect_tilt_get_reply(&dev->usb_motor, ctx);
		}
		else if (dev->model == KINECT_MODEL_1414) {
			uint8_t empty[0x1];

			angle = (angle<MIN_TILT_ANGLE) ? MIN_TILT_ANGLE : ((angle>MAX_TILT_ANGLE) ? MAX_TILT_ANGLE : angle);
			angle = angle * 2;
			
			ret = fnusb_control(&dev->usb_motor, 0x40, 0x31, (uint16_t)angle, 0x0, empty, 0x0);
		}			
	}


	return ret;
}

// translate between 1414 and K4W LEDS -
int freenect_led_translation [7][2] = {
	{0, 1},
	{1, 3},
	{2, 4},
	{3, 1},
	{4, 2},
	{5, 1},
	{6, 1}
};

int freenect_set_led(freenect_device *dev, freenect_led_options option)
{
	int ret = 0;
	freenect_context *ctx = dev->parent;

	option = freenect_led_translation[option][dev->model];

	if (dev->usb_motor.dev) {
		if (dev->model == KINECT_MODEL_K4W) {
			int transferred = 0;
			int res = 0;
			motor_command cmd;
			cmd.magic = fn_le32(0x06022009);
			cmd.tag = fn_le32(tag_seq++);
			cmd.arg1 = fn_le32(0);
			cmd.cmd = fn_le32(0x10);
			cmd.arg2 = (uint32_t)(fn_le32((int32_t)option));
			unsigned char buffer[20];
			memcpy(buffer, &cmd, 20);
			res = fnusb_bulk(&dev->usb_motor, 0x01, buffer, 20, &transferred);
			if (res != 0) {
				FN_ERROR("set_led(): lbulk_transfer failed: %d (transferred = %d)\n", res, transferred);
				return res;
			}
			ret = freenect_tilt_get_reply(&dev->usb_motor, ctx);
		}
		else if (dev->model == KINECT_MODEL_1414) {
			uint8_t empty[0x1];

			ret = fnusb_control(&dev->usb_motor, 0x40, 0x06, (uint16_t)option, 0x0, empty, 0x0);
		}
	}
	return ret;
}

double freenect_get_tilt_degs(freenect_raw_tilt_state *state)
{
	return ((double)state->tilt_angle) / 2.;
}

freenect_tilt_status_code freenect_get_tilt_status(freenect_raw_tilt_state *state)
{
	return state->tilt_status;
}

void freenect_get_mks_accel(freenect_raw_tilt_state *state, double* x, double* y, double* z)
{
	//the documentation for the accelerometer (http://www.kionix.com/Product%20Sheets/KXSD9%20Product%20Brief.pdf)
	//states there are 819 counts/g
	*x = (double)state->accelerometer_x/FREENECT_COUNTS_PER_G*GRAVITY;
	*y = (double)state->accelerometer_y/FREENECT_COUNTS_PER_G*GRAVITY;
	*z = (double)state->accelerometer_z/FREENECT_COUNTS_PER_G*GRAVITY;
}
