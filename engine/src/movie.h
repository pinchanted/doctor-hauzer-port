/* Movie playback: the game's FMV (OrgData/stream/xxDS, Cinepak + SDX2 on the 3DO), rebuilt offline by
 * tools/movies_build.py as MPEG-1 + MP2 (.mpg, 4x upscaled) and decoded here with pl_mpeg. */
#pragma once
#include <stdint.h>

typedef struct Movie Movie;

Movie *movie_open(const char *path);           /* NULL if the file is missing or unreadable */
int movie_update(Movie *m, double seconds);     /* decode; 0 once the movie has ended */
/* the newest frame (RGB, w*h*3); serial changes with every new frame */
int movie_frame(const Movie *m, const uint8_t **rgb, int *w, int *h, unsigned *serial);
double movie_time(const Movie *m);
double movie_duration(const Movie *m);
void movie_close(Movie *m);
