#include <stdio.h>
#include <string.h>
#include <libspotify/api.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <errno.h>

#include "queue.h"
#include "helpers.h"
#include "socket.h"
#include "audio.h"
#include "spotifyd.h"
#include "session.h"
#include "commandq.h"
#include "commandq.h"

pthread_t thread;

int main()
{
	sp_session *session = NULL;
	sp_error error;

	if(commandq_init() != 0)
	{
		printf("Couldn't create commandq.");
		exit(1);
	}

	audio_init(&g_audiofifo);

	pthread_mutex_init(&search_result_lock, NULL);

	queue_init();

	if((error = session_init(&session)) != SP_ERROR_OK)
	{
		printf("%s", sp_error_message(error));
	}

	int s = sock_create_un();
	
	/* Main loop. Process spotify events and incoming socket connections. */
	pthread_mutex_init(&notify_mutex, NULL);
	pthread_mutex_lock(&notify_mutex);
	notify_do = 1;
	int next_timeout = 0;
	for(;;)
	{
		struct timespec ts = rel_to_abstime(next_timeout);
		
		while(notify_do == 0)
		{
			int error = pthread_cond_timedwait(&notify_cond, &notify_mutex, &ts);
			if(error == ETIMEDOUT)
			{
				/* 
				 * This means next_timeout was reached.
				 * Time to get out of here and do stuff.
				 */
				break;
			}
		}
		notify_do = 0;	
		pthread_mutex_unlock(&notify_mutex);

		/* 
		 * A bit of a hack. Makes s2 an unsigned integer
		 * with the same length as a pointer. That means we
		 * can cast it to a void pointer and avoid heap-allocating
		 * an integer to send to the new thread.
		 */
		uintptr_t s2;
		struct sockaddr_un remote_un;
		unsigned remote_un_s = sizeof(remote_un);
		if( (s2 = accept(s, (struct sockaddr *) &remote_un, &remote_un_s)) != -1)
		{
			/* 
			 * we got someone connected. send them to the
			 * connection handler.
			 */
			pthread_create(&thread, NULL, sock_connection_handler, (void*) s2);
		}
	
		/* 
		 * Executes the command on the top of the command queue,
		 * if there is one.
		 */
		pthread_mutex_lock(&commandq_lock);
		commandq_execute_front(session);
		pthread_mutex_unlock(&commandq_lock);
		
		do
		{
			sp_session_process_events(session, &next_timeout);
		} while(next_timeout == 0);

		pthread_mutex_lock(&notify_mutex);
	}
	return 0;
}
