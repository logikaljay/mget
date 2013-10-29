#include <stdio.h>
#include <pthread.h>
#include <curl/curl.h>

static char **url;

static size_t get_size_struct(void *ptr, size_t size, size_t nmemb, void *data)
{
  (void)ptr;
  (void)data;
  /* we are not interested in the headers itself,
     so we only return the size we would have saved ... */ 
  return (size_t)(size * nmemb);
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
	//curl_easy_setopt(curl, CURLOPT_HEADER, 0L);
	res = curl_easy_perform(curl);
	res = curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &size);
	if (res!=CURLE_OK) {
		fprintf(stderr, "curl_easy_getinfo() failed: %s\n", curl_easy_strerror(res));
	}

	curl_easy_cleanup(curl);
	
	return size;
}

size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
	size_t written;
	written = fwrite(ptr, size, nmemb, stream);
	return written;
}

static void *get_part(void *range)
{
	CURL *curl;
	fprintf(stderr, "Fetching range: %s\n", range);
	FILE *file = fopen( "test" , "a");
	curl = curl_easy_init();
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_RANGE, range);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
	curl_easy_perform(curl);
	curl_easy_cleanup(curl);

	return NULL;
}

main(int argc, char **argv[])
{
	if (argc != 3) {
		printf("Incorrect parameters\nUsage: %s <num_parts> <url>\n", argv[0]);
		return -1;
	}

	url = argv[2];

	// setup our vars
	int parts = strtol(argv[1], &argv[1], 10);
	int i = 0;
	int remaining = 0;
	double partSize = 0;
	double segLocation = 0;

	// get file size
	double size = get_download_size(argv[2]);
	remaining = size;
	partSize = size / parts;

	fprintf(stderr, "file size: %0.0f\n", size);
	fprintf(stderr, "segment size: %0.0f\n", partSize);

	pthread_t tid[parts];
	int error;
	curl_global_init(CURL_GLOBAL_ALL);

	for (i=0; i<parts; i++) {
		double nextPart = 0;
		if (i == parts - 1) {
			nextPart = size;
		} else {
			nextPart = segLocation + partSize - 1;
		}

		char range[sizeof(segLocation) + sizeof(nextPart) + 1];
		sprintf(range, "%0.0f-%0.0f", segLocation, nextPart);

		error = pthread_create(&tid[i], NULL, get_part, (void *)range);
		if (0 != error) {
			fprintf(stderr, "Couldn't run thread %d, errno: %d\n", i, error);
		} else {
			//fprintf(stderr, "Thread %d, gets range %s\n", i, range);
		} 

		segLocation = segLocation + partSize;
	}

	// Wait for all threads to finish
	for (i=0; i<parts; i++) {
		error = pthread_join(tid[i], NULL);
		fprintf(stderr, "Thread %d finished\n", i);
	}

	// TODO join file.part{0..$PART} output filename

	// TODO remove all file.part{0..$PART}

	return 0;
}
