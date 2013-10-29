#include <stdio.h>
#include <string.h>
#include <math.h>
#include <curl/curl.h>

double toDownload = 0;
double totalDownloaded;
int handles = 0;

typedef struct progress {
	int handle;
	double totalDownloaded;
	double segmentSize;
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

int build_progress_bar()
{
	int totalDots = 40;
	double fractionDownloaded = totalDownloaded / toDownload;
	int dots = round(fractionDownloaded * totalDots);
	
	// create the meter
	int ii = 0;
	printf("%3.0f%% [", fractionDownloaded*100);
	
	// part thats downloaded
	for ( ; ii < dots; ii++) {
		printf("=");
	}
	
	for ( ; ii < totalDots; ii++) {
		printf(" ");
	}
	
	printf("]\r");
	fflush(stdout);
	
	return 0;
}

int display_progress()
{
	int i = 0;
	for ( ; i < handles; i++) {
		struct progress pgr = MyProgress[i];
		int totaldots = 40;
		double totalDownloaded = pgr.totalDownloaded;
		double segmentSize = pgr.segmentSize;
		double fractionDownloaded = totalDownloaded / segmentSize;
		printf("handle %d: %0.0f / %0.0f\n", i, totalDownloaded, segmentSize);
	}
	printf("\n");
	
	return 0;
}

int progress_func(dl_progress ptr, double totalToDownload, double nowDownloaded, double totalToUpload, double nowUploaded)
{
	int currentHandle = ptr->handle;
	ptr->totalDownloaded = nowDownloaded;
	
	//printf("pdata->handle: %d = %0.0f/%0.0f\n", ptr->handle, ptr->totalDownloaded, ptr->segmentSize);
	
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

	// setup our vars
	const char *outputfile;
	char **url = argv[2];
	int parts = strtol(argv[1], &argv[1], 10); // base 10
	double partSize = 0;
	double segLocation = 0;
	int still_running;
	handles = parts;
	int i; // iterator

	// get file name
	outputfile = strrchr((const char *)url, '/') + 1;
	
	// get file size
	double size = get_download_size(argv[2]);
	toDownload = size;
	partSize = size / parts;

	// output some file size/segement size info
	fprintf(stderr, "file size: %0.0f\n", size);
	fprintf(stderr, "segment size: %0.0f\n", partSize);

	// setup curl vars
	FILE *fileparts[parts];
	CURL *handles[parts];
	CURLM *multi_handle;
	CURLMsg *msg;
	int msgs_left;

	int error;
	curl_global_init(CURL_GLOBAL_ALL);

	for (i=0; i<parts; i++) {
	
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
			case 0: /* timeout */
			default:
				// action
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
			
			//fprintf(stderr, "Segment %d has finished\n", index);
		}
	}
	
	// clean up our multi handle
	curl_multi_cleanup(multi_handle);
	
	// free up the curl handles
	for (i=0; i<parts; i++) {
		curl_easy_cleanup(handles[i]);
	}

	return 0;
}
