/* Room files (OrgData/room/roomNNN.bin) and the shared mesh format. Port of tools/room.py;
 * layout notes in docs/FORMATS.md. Coordinates: y down, floor at +200, 256 angle units per turn. */
#pragma once
#include <stdint.h>
#include "util.h"

typedef struct { int n, tex, v[8]; } Face;

typedef struct {
    int nverts, nfaces;
    float (*verts)[3];
    Face *faces;
} Mesh;

typedef struct {
    int id;              /* 100, 101, ... (actor handle) */
    Mesh mesh;
    float pos[3];
    float rot[3];        /* 256 per turn */
    int table[16];       /* face tex -> texture bank index */
    int flags64, draw68, nocull6c;
    int collidable, trigger, area, visible;
} RoomObject;

typedef struct { float eye[3], target[3]; } RoomCamera;

typedef struct {
    Blob file;
    int nobjects;
    RoomObject *objects;          /* [0] = shell */
    int ntex;
    uint32_t *tex_off, *tex_size; /* absolute, into file.data */
    int zones[400], triggers[400];
    int ncams;
    RoomCamera cams[64];
} Room;

int room_load(Room *r, const char *path);
void room_free(Room *r);
int mesh_read(const uint8_t *d, size_t n, size_t off, Mesh *m);   /* 0 on success */
void mesh_free(Mesh *m);
void obj_transform(const float pos[3], const float rot[3], const float in[3], float out[3]);
int room_zone_at(const Room *r, float x, float z);                 /* -1 outside */
