#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "jsapi-binding.h"

#define HDR_COMMENT_PREABLE "/* Generated by nsgenjsapi\n"

static FILE *outfile = NULL;
static char *hdr_comments = NULL;
static bool hdr_comments_output = false;

int genjsbind_outputopen(char *outfilename)
{
        /* open output file */
	if (outfilename == NULL) {
		outfile = stdout;
	} else {
		outfile = fopen(outfilename, "w");
	}

	if (!outfile) {
		fprintf(stderr, "Error opening output %s: %s\n",
			outfilename, 
			strerror(errno));
		return 4;
	}

	hdr_comments = strdup(HDR_COMMENT_PREABLE);
	return 0;
}

int genjsbind_outputclose()
{
	fclose(outfile);
	return 0;
}

int genjsbind_header_comment(char *comment)
{
	char *fullstr;
	int fulllen;
	fulllen = strlen(hdr_comments) + strlen(comment) + 2;
	fullstr = malloc(fulllen);
	snprintf(fullstr, fulllen, "%s\n%s", hdr_comments , comment);
	free(hdr_comments);
	free(comment);
	hdr_comments  = fullstr;

	if (hdr_comments_output) {
		fprintf(stderr, "Warning: adding header comments after output already started\n");
	}
	return 0;
}

int genjsbind_output_interface(const char *interface)
{
	if (!hdr_comments_output) {
		fprintf(outfile, "%s\n*/\n\n", hdr_comments);
		hdr_comments_output = true;
	}
	fprintf(outfile, "/* interface %s */\n\n", interface);
	return 0;
}
