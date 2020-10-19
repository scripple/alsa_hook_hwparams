/*
 *  ALSA pcm hook functions - for use with loopback device
 *  since pcm_notify is broken.
 *  
 *  Based on the pcm_hook.c code from alsa-lib
 *
 *  Copyright (c) 2001 by Abramo Bagnara <abramo@alsa-project.org>
 *
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Lesser General Public License as
 *   published by the Free Software Foundation; either version 2.1 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */
#include <alsa/asoundlib.h>

static int snd_pcm_hook_hwparams_hw_params(snd_pcm_hook_t *hook)
{
	snd_pcm_t *pcm = NULL;
	snd_pcm_info_t *info = NULL;

	int err, card;
	unsigned int device, subdevice;
	char filename[51];
	char command[1000];
	FILE *hwparams_file = NULL;
	char line[100];
	char format[21];
	int channels = -1;
	int rate = -1;

	const char **commands = (const char **)snd_pcm_hook_get_private(hook);
	pcm = snd_pcm_hook_get_pcm(hook);
	if (!pcm) {
		return -EINVAL;
	}

	snd_pcm_info_malloc(&info);
	err = snd_pcm_info(pcm, info);
	if (err) {
		snd_pcm_info_free(info);
		return err;
	}

	card = snd_pcm_info_get_card(info);
	if (card < 0) {
		snd_pcm_info_free(info);
		return card;
	}
	device = snd_pcm_info_get_device(info);
	subdevice = snd_pcm_info_get_subdevice(info);
	snd_pcm_info_free(info);

	// Annoyingly it seems alsa decided not to give us the hw_params
	// stashing them in the generic pcm slave.
	// Parse them from proc
	snprintf(filename, 50, "/proc/asound/card%d/pcm%up/sub%u/hw_params",
			card, device, subdevice);
	hwparams_file = fopen(filename, "r"); 
	if (!hwparams_file) {
		return -EINVAL;
	}
	while (fgets(line, sizeof(line), hwparams_file)) {
		sscanf(line, "format: %20s", format);
		sscanf(line, "channels: %d", &channels);
		sscanf(line, "rate: %d", &rate);
	} 
	fclose(hwparams_file);

	// Send the open command as hw_params are ready
	// Command will be called with arguments "format rate channels"
	snprintf(command, 1000, "%s %s %d %d\n", commands[0], format, rate, channels);
	err = system(command);
	if (err > 0)
		return -err;
	else
		return err;
}

static int snd_pcm_hook_hwparams_hw_free(snd_pcm_hook_t *hook)
{
	const char **commands = (const char **)snd_pcm_hook_get_private(hook);
	// Send the close command so loopback can be opened / set with
	// new hardware parameters
	int err = system(commands[1]);
	if (err > 0)
		return -err;
	else
		return err;
}

static int snd_pcm_hook_hwparams_close(snd_pcm_hook_t *hook)
{
	const char **commands = (const char **)snd_pcm_hook_get_private(hook);
	free((void *)commands[0]);
	free((void *)commands[1]);
	free((void *)commands);
	snd_pcm_hook_set_private(hook, NULL);
	return 0;
}

int _snd_pcm_hook_hwparams_install(snd_pcm_t *pcm, snd_config_t *conf)
{
	int err = 0;
	const char *temp = NULL;
	char **commands = NULL;
	snd_config_t *pcm_command = NULL;
	snd_pcm_hook_t *h_hw_params = NULL, *h_hw_free = NULL, *h_close = NULL;

	assert(conf);
	assert(snd_config_get_type(conf) == SND_CONFIG_TYPE_COMPOUND);

	commands = (char **)malloc(2*sizeof(char *));

	/* Parse configuration options from asoundrc */
	err = snd_config_search(conf, "opencmd", &pcm_command);
	if (err < 0) {
		SNDERR("opencmd not found in ALSA config.");
		err = -EINVAL;
		goto _err;
	}
	snd_config_get_string(pcm_command, &temp);
	commands[0] = (char *)malloc(strlen(temp)+1);
	if (!commands[0]) {
		SNDERR("Out of memory");
		err =  -ENOMEM;
		goto _err;
	}
	strncpy(commands[0], temp, strlen(temp)+1);

	err = snd_config_search(conf, "closecmd", &pcm_command);
	if (err < 0) {
		SNDERR("closecmd not found in ALSA config.");
		err = -EINVAL;
		goto _err;
	}
	snd_config_get_string(pcm_command, &temp);
	commands[1] = (char *)malloc(strlen(temp)+1);
	if (!commands[1]) {
		SNDERR("Out of memory");
		err = -ENOMEM;
		goto _err;
	}
	strncpy(commands[1], temp, strlen(temp)+1);
  
	err = snd_pcm_hook_add(&h_hw_params, pcm, SND_PCM_HOOK_TYPE_HW_PARAMS,
			       snd_pcm_hook_hwparams_hw_params, (void *)commands);
	if (err < 0)
		goto _err;
	err = snd_pcm_hook_add(&h_hw_free, pcm, SND_PCM_HOOK_TYPE_HW_FREE,
			       snd_pcm_hook_hwparams_hw_free, (void *)commands);
	if (err < 0)
		goto _err;
	err = snd_pcm_hook_add(&h_close, pcm, SND_PCM_HOOK_TYPE_CLOSE,
			       snd_pcm_hook_hwparams_close, (void *)commands);
	if (err < 0)
		goto _err;

	// In case loopback capture was already open send the close command
	// so that the playback program will succeed if run a second time
	err = system(commands[1]);
	if (err == 0) return 0;

	if (err > 0) err = -err;
 _err:
	if (commands[0])
		free((void *)commands[0]);
	if (commands[1])
		free((void *)commands[1]);
	if (commands)
		free((void *)commands);
	if (h_hw_params)
		snd_pcm_hook_remove(h_hw_params);
	if (h_hw_free)
		snd_pcm_hook_remove(h_hw_free);
	if (h_close)
		snd_pcm_hook_remove(h_close);
	return err;
}
SND_DLSYM_BUILD_VERSION(_snd_pcm_hook_hwparams_install, SND_PCM_DLSYM_VERSION);
