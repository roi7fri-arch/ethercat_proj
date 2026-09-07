#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/joystick.h>
#include <pthread.h>
#include "joystick_eth.h"


short el_torque_val = 0;
pthread_mutex_t el_mutex;
pthread_mutex_t tr_mutex;
short tr_torque_val = 0;


int open_joystick()
{
	int fd;
	fd = open("/dev/input/js0", O_RDONLY);
	return fd;
}

short get_el_torque_val()
{
	//pthread_mutex_lock(&el_mutex);
	return el_torque_val;
	//pthread_mutex_unlock(&el_mutex);
}

short get_tr_torque_val()
{
	//pthread_mutex_lock(&tr_mutex);
	return tr_torque_val;
	//pthread_mutex_unlock(&tr_mutex);
}

void joy_normalization(short value, int axis_num)
{
	short normal_amp = 0;
	if(EL_AXIS == axis_num)
	{
		normal_amp = (value * MAX_AMP) / JOY_MAX_VAL;
//		pthread_mutex_lock(&el_mutex);
		el_torque_val = normal_amp;
//		pthread_mutex_unlock(&el_mutex);

	}
	else if(TR_AXIS == axis_num)
	{
		normal_amp = (value * MAX_AMP) / JOY_MAX_VAL;
//		pthread_mutex_lock(&tr_mutex);
		tr_torque_val = normal_amp;
//		pthread_mutex_unlock(&tr_mutex);
	}
}

void* run(void* data)
{
	int fd;
	int res;
	struct js_event e;
	int event_size = 0;
	printf("starting joystick thread\n");
	fd = open("/dev/input/js0", O_RDONLY);
	if(fd < 0)
	{
		printf("failed to open joystick device\n");
		pthread_exit(NULL);
	}
	res = pthread_mutex_init(&el_mutex, NULL);
	res += pthread_mutex_init(&tr_mutex, NULL);
	if(res)
	{
		printf("failed to init mutex\n");
		pthread_exit(NULL);
	}
	while(1)
	{
		event_size = read(fd, &e, sizeof(e));
		if(event_size != sizeof(e))
		{
			printf("joystick event read failed\n");
		}
		switch(e.type)
		{
			case JS_EVENT_BUTTON:
				if(e.value)
				{
					printf("button number %d pressed\n", e.number);
				}
				else
				{
					printf("button number %d released\n", e.number);
				}
				break;
			case JS_EVENT_AXIS:
				joy_normalization(e.value, e.number);
				break;
			case JS_EVENT_INIT:
				printf("event init\n");
				break;
			dafault:
				printf("event value is %d\n", e.type);
		}
	}
}
