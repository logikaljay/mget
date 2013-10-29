#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ncurses.h>
#include <time.h>
#include <curl/curl.h>

WINDOW *win;
double toDownload = 0;
double totalDownloaded;
int handles = 0;
int startx, starty, width, height;
int ch;

WINDOW *create_newwin(int height, int width, int starty, int startx);

void destroy_win(WINDOW *local_win);

typedef struct progress {
	int handle;
	double totalDownloaded;
	double segmentSize;
	double currentTime;
	double startTime;
} *dl_progress;

struct progress MyProgress[];

size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) 
{
	size_t written;
	written = fwrite(ptr, size, nmemb, stream);
	return written;
}

static size_t get_size_struct(void *ptr, size_t size, size_t nmemb, void *data)
{
	(void)ptr;
	(void)data;

	// return only the size, dump the rest
	return (size_t)(size * nmemb);
}

int display_progress()
{
	int i = 0;
	mvwprintw(win, 0, 2, "Downloading %dx%0.0f MB segments (%0.2f MB)", handles, toDownload / handles / 1024 / 1024, toDownload / 1024 / 1024);
	
	for ( ; i < handles; i++) {
		int totalDots = 40;
		struct progress pgr = MyProgress[i];
		double totalDownloaded = pgr.totalDownloaded;
		double segmentSize = pgr.segmentSize;
		double fractionDownloaded = totalDownloaded / segmentSize;
		double averageSpeed = pgr.totalDownloaded / (pgr.currentTime - pgr.startTime);
		int dots = round(fractionDownloaded * totalDots);
		
		// create the meter
		int ii = 0;
		for ( ; ii < dots; ii++)
			mvwprintw(win, i + 3, 8 + ii, "=");
		for ( ; ii < totalDots; ii++)
			mvwprintw(win, i + 3, 8 + ii, " ");
		mvwprintw(win, i + 3, totalDots + 8, "]");
		mvwprintw(win, i + 3, 2, "%3.0f%% [", fractionDownloaded * 100);
		
		// display some download info
		mvwprintw(win, i + 3, totalDots + 10, "%03.2f KB/s", averageSpeed / 1024);
		mvwprintw(win, i + 3, width - 18, "%0.2f / %0.2f MB", totalDownloaded / 1024 / 1024, segmentSize / 1024 / 1024);
	}
	
	double speedSum = 0;
	double downloadSum = 0;
	for (i=0; i<handles; i++) {
		struct progress pgr = MyProgress[i];
		double totalDownloaded = pgr.totalDownloaded;
		double segmentSize = pgr.segmentSize;
		double fractionDownloaded = totalDownloaded / segmentSize;
		double averageSpeed = averageSpeed = pgr.totalDownloaded / (pgr.currentTime - pgr.startTime);
		speedSum += averageSpeed;
		downloadSum += totalDownloaded;
		
		// display total info
		mvwprintw(win, height - 1, 2, "Average Speed: %3.2f KB/s" , speedSum / 1024); 
		mvwprintw(win, height - 1, width - sizeof(downloadSum / 1024 / 1204) - 17 - 2, "Total Downloaded: %000.2f MB", downloadSum / 1024 / 1204);
	}
	
	wrefresh(win);
	
	return 0;
}

int progress_func(dl_progress ptr, double totalToDownload, double nowDownloaded, double totalToUpload, double nowUploaded)
{
	time_t seconds;
	seconds = time (NULL);
	int currentHandle = ptr->handle;
	ptr->totalDownloaded = nowDownloaded;
	ptr->currentTime = seconds;
	
	return display_progress();
}

static double get_download_size(char** url)
{
	CURL *curl;
	CURLcode res;
	double size = 0.0;

	curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, get_size_struct);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	res = curl_easy_perform(curl);
	res = curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &size);
	if (res!=CURLE_OK) {
		fprintf(stderr, "curl_easy_getinfo() failed: %s\n", curl_easy_strerror(res));
	}

	curl_easy_cleanup(curl);
	
	return size;
}

main(int argc, char **argv[])
{
	if (argc != 3) {
		printf("Incorrect parameters\nUsage: %s <num_parts> <url>\n", argv[0]);
		return -1;
	}
	
	// setup the start time for average download speed later when we have finished
	time_t startTime;
	startTime = time (NULL);
	
	// number of parts
	int parts = strtol(argv[1], &argv[1], 10); // base 10
	
	// create the window
	initscr();
	height = parts + 5;
	width = 80;
	starty = (LINES - height) / 2;
	startx = (COLS - width) / 2;
	refresh();
	win = create_newwin(height, width, starty, startx);
	
	// setup our vars
	const char *outputfile;
	char **url = argv[2];
	double partSize = 0;
	double segLocation = 0;
	int still_running;
	handles = parts;
	int i; 

	// get file name
	outputfile = strrchr((const char *)url, '/') + 1;
	
	// get file size
	double size = get_download_size(argv[2]);
	toDownload = size;
	partSize = size / parts;

	// setup curl vars
	FILE *fileparts[parts];
	CURL *handles[parts];
	CURLM *multi_handle;
	CURLMsg *msg;
	int msgs_left;

	int error;
	curl_global_init(CURL_GLOBAL_ALL);

	for (i=0; i<parts; i++) {
		time_t seconds;
		seconds = time (NULL);
		MyProgress[i].startTime = seconds;
		MyProgress[i].handle = i;
		MyProgress[i].segmentSize = partSize;
		MyProgress[i].totalDownloaded = 0;
		
		// setup our output filename
		char filename[50];
		sprintf(filename, "%s.part.%0d", outputfile, i);
		
		// allocate curl handle for each segment
		handles[i] = curl_easy_init();
		fileparts[i] = fopen(filename, "w");
		totalDownloaded = 0;
		
		double nextPart = 0;
		if (i == parts - 1) {
			nextPart = size;
		} else {
			nextPart = segLocation + partSize - 1;
		}

		char range[sizeof(segLocation) + sizeof(nextPart) + 1];
		sprintf(range, "%0.0f-%0.0f", segLocation, nextPart);

		// set some curl options.
		curl_easy_setopt(handles[i], CURLOPT_URL, url);
		curl_easy_setopt(handles[i], CURLOPT_RANGE, range);
		curl_easy_setopt(handles[i], CURLOPT_WRITEFUNCTION, write_data);
		curl_easy_setopt(handles[i], CURLOPT_NOPROGRESS, 0);
		curl_easy_setopt(handles[i], CURLOPT_PROGRESSFUNCTION, progress_func);
		curl_easy_setopt(handles[i], CURLOPT_PROGRESSDATA, &MyProgress[i]);
		curl_easy_setopt(handles[i], CURLOPT_WRITEDATA, fileparts[i]);

		segLocation = segLocation + partSize;
	}

	multi_handle = curl_multi_init();
	
	// add all individual transfers to the stack
	for (i=0; i<parts; i++) {
		curl_multi_add_handle(multi_handle, handles[i]);
	}
	
	curl_multi_perform(multi_handle, &still_running);
	
	do {
		struct timeval timeout;
		int rc; // return code
		fd_set fdread;
		fd_set fdwrite;
		fd_set fdexcep; // file descriptor exception
		int maxfd = -1;
		
		long curl_timeo = -1;
		
		FD_ZERO(&fdread);
		FD_ZERO(&fdwrite);
		FD_ZERO(&fdexcep);
		
		// set a suitable timeout to play with
		timeout.tv_sec = 100 * 1000;
		timeout.tv_usec = 0;
		
		curl_multi_timeout(multi_handle, &curl_timeo);
		if (curl_timeo >= 0) {
			timeout.tv_sec = curl_timeo / 1000;
			if (timeout.tv_sec > 1) {
				timeout.tv_sec = 1;
			} else {
				timeout.tv_usec = (curl_timeo % 1000) * 1000;
			}
		}
		
		curl_multi_fdset(multi_handle, &fdread, &fdwrite, &fdexcep, &maxfd);
		
		rc = select(maxfd+1, &fdread, &fdwrite, &fdexcep, &timeout);
		
		switch (rc) {
			case -1:
				fprintf(stderr, "Could not select the error\n");
				break;
			case 0: 
				/* timeout */
			default:
				curl_multi_perform(multi_handle, &still_running);
				break;	
		}
	} while(still_running);
	
	while ((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
		if (msg->msg == CURLMSG_DONE) {
			int index, found = 0;
			
			for (index = 0; index<parts; index++) {
				found = (msg->easy_handle == handles[index]);
				if (found)
					break;
			}
		}
	}
	
	// clean up our multi handle
	curl_multi_cleanup(multi_handle);
	
	// free up the curl handles
	for (i=0; i<parts; i++) {
		curl_easy_cleanup(handles[i]);
	}
	
	// close ncurses window
	endwin();
	
	// send some output to the console for records sake.
	time_t endTime;
	endTime = time (NULL);
	printf("Downloaded %0.2f MB in %d seconds\n", partSize * parts / 1024 / 1024, endTime - startTime);
	printf("%0.2f KB/s (average)\n", (partSize * parts / (endTime - startTime) / 1024));

	return 0;
}

WINDOW *create_newwin(int height, int width, int starty, int startx)
{	WINDOW *local_win;

	local_win = newwin(height, width, starty, startx);
	box(local_win, 0 , 0);
	wrefresh(local_win);

	return local_win;
}

void destroy_win(WINDOW *local_win)
{	
	/* box(local_win, ' ', ' '); : This won't produce the desired
	 * result of erasing the window. It will leave it's four corners 
	 * and so an ugly remnant of window. 
	 */
	wborder(local_win, '|', '|', '-','-','+','+','+','+');
	/* The parameters taken are 
	 * 1. win: the window on which to operate
	 * 2. ls: character to be used for the left side of the window 
	 * 3. rs: character to be used for the right side of the window 
	 * 4. ts: character to be used for the top side of the window 
	 * 5. bs: character to be used for the bottom side of the window 
	 * 6. tl: character to be used for the top left corner of the window 
	 * 7. tr: character to be used for the top right corner of the window 
	 * 8. bl: character to be used for the bottom left corner of the window 
	 * 9. br: character to be used for the bottom right corner of the window
	 */
	wrefresh(local_win);
	delwin(local_win);
}