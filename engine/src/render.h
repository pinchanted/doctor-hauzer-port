/* GL renderer (GLES2 feature level: desktop GL 2.1, GLES 2/3, vitaGL). */
#pragma once
#include "human.h"
#include "room.h"

typedef struct Renderer Renderer;

typedef struct {
    float eye[3], target[3];   /* a room camera as stored; the game's eye offset is applied inside */
} CameraView;

Renderer *render_create(int gles, const char *hd_dir);
void render_destroy(Renderer *r);
void render_set_room(Renderer *r, const Room *room);          /* uploads meshes + textures */
void render_set_human(Renderer *r, const HumanModel *h);      /* uploads player textures */
void render_frame(Renderer *r, const Room *room, int area, const CameraView *cam,
                  const TriList *player, int w, int h);
int render_screenshot(int w, int h, const char *path);
extern float GAME_FOV_DEG;
