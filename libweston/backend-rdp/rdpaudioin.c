/*
 * Copyright © 2020 Microsoft
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include "config.h"

#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/un.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <linux/vm_sockets.h>
#include <libweston/libweston.h>
#include <shared/xalloc.h>

#include "rdp.h"

static AUDIO_FORMAT rdp_audioin_supported_audio_formats[] = {
	{ WAVE_FORMAT_PCM, 1, 44100, 88200, 2, 16, 0, NULL },
};

static int
rdp_audioin_setup_listener(struct audio_in_private *priv)
{
	char *source_socket_path;
	int fd;
	struct sockaddr_un s;
	int bytes;
	int error;

	fd = socket(PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) {
		weston_log("Couldn't create audioin listener socket.\n");
		return -1;
	}

	source_socket_path = getenv("PULSE_AUDIO_RDP_SOURCE");
	if (source_socket_path == NULL || source_socket_path[0] == '\0') {
		close(fd);
		weston_log("Environment variable PULSE_AUDIO_RDP_SOURCE not set.\n");
		return -1;
	}

	memset(&s, 0, sizeof(s));
	s.sun_family = AF_UNIX;
	bytes = sizeof(s.sun_path) - 1;
	snprintf(s.sun_path, bytes, "%s", source_socket_path);

	remove(s.sun_path);

	rdp_debug(priv, "Pulse Audio source listener socket on %s\n", s.sun_path);
	error = bind(fd, (struct sockaddr *)&s, sizeof(struct sockaddr_un));
	if (error != 0) {
		close(fd);
		weston_log("Failed to bind to listener socket for audioin (%d).\n", error);
		return -1;
	}
    
	listen(fd, 100);
	return fd;
}


static UINT
rdp_audioin_data(
	audin_server_context* context,
	const SNDIN_DATA* data)
{
	AUDIO_FORMAT* format = audin_server_get_negotiated_format(context);
	struct audio_in_private *priv = context->userdata;
	wStream* buf = data->Data;

	if (!priv->isAudioInStreamOpened || priv->pulseAudioSourceFd == -1) {
		weston_log("RDPAudioIn - audio stream is not opened.\n");
		return 0;
	}

	assert(format->wFormatTag == WAVE_FORMAT_PCM);
	assert(format->nChannels == 1);
	assert(format->nSamplesPerSec == 44100);
	assert(format->wBitsPerSample == 16);
	assert(buf != NULL);

	int bytes = buf->length;
	int sent = send(priv->pulseAudioSourceFd, buf->buffer, bytes, 0);
	if (sent != bytes) {
		rdp_debug(priv, "RDP AudioIn source send failed (sent:%d, bytes:%d) %s\n",
				sent, bytes, strerror(errno));

		/* Unblock worker thread to close pipe to pulseaudio */
		uint64_t one=1;
		if (write(priv->closeAudioSourceFd, &one, sizeof(one)) != sizeof(uint64_t)) {
			weston_log("RDP AudioIn error at receive_samples while writing to closeAudioSourceFd (%s)\n", strerror(errno));
			return ERROR_INTERNAL_ERROR;
		}

		if (sent <= 0) {
			/* return error to FreeRDP as failed to send samples to pulseaudio. */
			return ERROR_INTERNAL_ERROR;
		}
	}

	return 0;
}

static void*
rdp_audioin_source_thread(void *context)
{
	struct audio_in_private *priv = context;

	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
 
	assert(priv->closeAudioSourceFd != -1);
	assert(priv->pulseAudioSourceListenerFd != -1); 
    
	for (;;) {
		rdp_debug(priv, "AudioIn source_thread: Listening for audio in connection.\n");

		if (priv->audioInExitSignal) {
			rdp_debug(priv, "AudioIn source_thread is asked to exit (accept loop)\n");
			break;
		}

		/*
		 * Wait for a connection on our listening socket
		 */   
		priv->pulseAudioSourceFd = accept(priv->pulseAudioSourceListenerFd, NULL, NULL);
		if (priv->pulseAudioSourceFd < 0) {
			weston_log("AudioIn source thread: Listener connection error (%s)\n", strerror(errno));
			continue;
		} else {
			rdp_debug(priv, "AudioIn connection successful on socket (%d).\n", priv->pulseAudioSourceFd);
			if (priv->audin_server_context->Open(priv->audin_server_context)) {
				rdp_debug(priv, "RDP AudioIn opened.\n");
				/*
				 * Wait for the connection to be closed
				 */
				uint64_t dummy;
				if (read(priv->closeAudioSourceFd, &dummy, sizeof(dummy)) != sizeof(uint64_t)) {
					weston_log("RDP AudioIn wait on eventfd failed. thread exiting. %s\n", strerror(errno));
					break;
				}
				priv->audin_server_context->Close(priv->audin_server_context);
				rdp_debug(priv, "RDP AudioIn closed.\n");
			} else {
				weston_log("Failed to open audio in connection with RDP client.\n");
			}

			close(priv->pulseAudioSourceFd);
			priv->pulseAudioSourceFd = -1;
		}
	}

	if (priv->audin_server_context->IsOpen(priv->audin_server_context))
		priv->audin_server_context->Close(priv->audin_server_context);

	if (priv->pulseAudioSourceFd != -1) {
		close(priv->pulseAudioSourceFd);
		priv->pulseAudioSourceFd = -1;
	}

	return NULL;
}

void *
rdp_audio_in_init(struct weston_compositor *c, HANDLE vcm)
{
	struct audio_in_private *priv;
	priv = xzalloc(sizeof *priv);
	priv->audin_server_context = audin_server_context_new(vcm);
	if (!priv->audin_server_context) {
		weston_log("RDPAudioIn - Couldn't initialize audio virtual channel.\n");
		return NULL;
	}
        priv->debug = weston_compositor_add_log_scope(c, "rdp-audio-in",
                                                      "Debug messages for RDP audio input\n",
                                                      NULL, NULL, NULL);

	priv->audioInExitSignal = FALSE;
	priv->pulseAudioSourceThread = 0;
	priv->pulseAudioSourceListenerFd = -1;
	priv->pulseAudioSourceFd = -1;
	priv->closeAudioSourceFd = -1;

	// this will be freed by FreeRDP at audin_server_context_free.
	AUDIO_FORMAT *audio_formats = malloc(sizeof rdp_audioin_supported_audio_formats);
	if (!audio_formats) {
		weston_log("RDPAudioIn - Couldn't allocate memory for audio formats.\n");
		goto Error_Exit;
	}
	memcpy(audio_formats,
			rdp_audioin_supported_audio_formats,
			sizeof rdp_audioin_supported_audio_formats);

    // Configure
	if(!audin_server_set_formats(
                priv->audin_server_context,
                ARRAYSIZE(rdp_audioin_supported_audio_formats),
                audio_formats)) {
		weston_log("RDPAudioIn - Failed to call audin_server_set_formats.\n");
		goto Error_Exit;
    }
	priv->audin_server_context->userdata = (void*)priv;
	priv->audin_server_context->Data = rdp_audioin_data;

	priv->closeAudioSourceFd = eventfd(0, EFD_CLOEXEC);
	if (priv->closeAudioSourceFd < 0) {
		weston_log("RDPAudioIn - Couldn't initialize eventfd.\n");
		goto Error_Exit;
	}

	priv->pulseAudioSourceListenerFd = rdp_audioin_setup_listener(priv);
	if (priv->pulseAudioSourceListenerFd < 0) {
		weston_log("RDPAudioIn - rdp_audioin_setup_listener failed.\n");
		goto Error_Exit;
	}

	if (pthread_create(&priv->pulseAudioSourceThread, NULL, rdp_audioin_source_thread, (void*)priv) < 0) {
		weston_log("RDPAudioIn - Failed to start Pulse Audio Source Thread. No audio in will be available.\n");
		goto Error_Exit;
	}

	return priv;

Error_Exit:
	if (priv->debug)
		weston_log_scope_destroy(priv->debug);

	if (priv->pulseAudioSourceListenerFd != -1) {
		close(priv->pulseAudioSourceListenerFd);
		priv->pulseAudioSourceListenerFd = -1;
	}

	if (priv->closeAudioSourceFd != -1) {
		close(priv->closeAudioSourceFd);
		priv->closeAudioSourceFd = -1;
	}

	if (priv->audin_server_context) {
		audin_server_context_free(priv->audin_server_context);
		priv->audin_server_context = NULL;
	}
	free(priv);

	return NULL; // Continue without audio
}

void
rdp_audio_in_destroy(void *audio_in_private)
{
	struct audio_in_private *priv = audio_in_private;
	if (priv->audin_server_context) {

		if (priv->pulseAudioSourceThread) {
			priv->audioInExitSignal = TRUE;
			shutdown(priv->pulseAudioSourceListenerFd, SHUT_RDWR);
			shutdown(priv->closeAudioSourceFd, SHUT_RDWR);
			pthread_cancel(priv->pulseAudioSourceThread);
			pthread_join(priv->pulseAudioSourceThread, NULL);

			if (priv->pulseAudioSourceListenerFd != -1) {
				close(priv->pulseAudioSourceListenerFd);
				priv->pulseAudioSourceListenerFd = -1;
			}

			if (priv->closeAudioSourceFd != -1) {
				close(priv->closeAudioSourceFd);
				priv->closeAudioSourceFd = -1;
			}

			priv->pulseAudioSourceThread = 0;
		}

		assert(priv->pulseAudioSourceListenerFd < 0);
		assert(priv->closeAudioSourceFd < 0);

		assert(!priv->audin_server_context->IsOpen(priv->audin_server_context));
		audin_server_context_free(priv->audin_server_context);
		priv->audin_server_context = NULL;
	}
	free(priv);
}
