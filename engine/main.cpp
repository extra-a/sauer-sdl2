// main.cpp: initialisation & main loop

#include "engine.h"
#include "controllerdb.h"

extern void cleargamma();

void cleanup()
{
    recorder::stop();
    cleanupserver();
    SDL_ShowCursor(SDL_TRUE);
    SDL_SetRelativeMouseMode(SDL_FALSE);
    if(screen) SDL_SetWindowGrab(screen, SDL_FALSE);
    cleargamma();
    freeocta(worldroot);
    extern void clear_command(); clear_command();
    extern void clear_console(); clear_console();
    extern void clear_mdls();    clear_mdls();
    extern void clear_sound();   clear_sound();
    closelogfile();
    SDL_Quit();
}

void quit()                     // normal exit
{
    extern void writeinitcfg();
    writeinitcfg();
    writeservercfg();
    abortconnect();
    disconnect();
    localdisconnect();
    writecfg();
    writeextendedcfg();
    cleanup();
    exit(EXIT_SUCCESS);
}

#if !defined(WIN32) && !defined(_DEBUG) && defined(__GNUC__)
#include <stdio.h>
#include <execinfo.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

void handler(int sig) {
  void *array[10];
  size_t size;
  size = backtrace(array, 10);
  fprintf(stderr, "Error: signal %d:\n", sig);
  backtrace_symbols_fd(array, size, STDERR_FILENO);
  exit(1);
}

#endif

void fatal(const char *s, ...)    // failure exit
{
    static int errors = 0;
    errors++;

    if(errors <= 2) // print up to one extra recursive error
    {
        defvformatstring(msg,s,s);
        logoutf("%s", msg);

        if(errors <= 1) // avoid recursion
        {
            if(SDL_WasInit(SDL_INIT_VIDEO))
            {
                SDL_ShowCursor(SDL_TRUE);
                SDL_SetRelativeMouseMode(SDL_FALSE);
                if(screen) SDL_SetWindowGrab(screen, SDL_FALSE);
                cleargamma();
            }
            #ifdef WIN32
                MessageBox(NULL, msg, "Cube 2: Sauerbraten fatal error", MB_OK|MB_SYSTEMMODAL);
            #endif
            SDL_Quit();
        }
    }

    exit(EXIT_FAILURE);
}

SDL_Window *screen = NULL;
int screenw = 0, screenh = 0, desktopw = 0, desktoph = 0;
SDL_GLContext glcontext = NULL;

int curtime = 0, lastmillis = 1, elapsedtime = 0, totalmillis = 1;

dynent *player = NULL;

int initing = NOT_INITING;

bool initwarning(const char *desc, int level, int type)
{
    if(initing < level) 
    {
        addchange(desc, type);
        return true;
    }
    return false;
}

#define SCR_MINW 320
#define SCR_MINH 200
#define SCR_MAXW 10000
#define SCR_MAXH 10000
#define SCR_DEFAULTW 1024
#define SCR_DEFAULTH 768
VARF(scr_w, SCR_MINW, -1, SCR_MAXW, initwarning("screen resolution"));
VARF(scr_h, SCR_MINH, -1, SCR_MAXH, initwarning("screen resolution"));
VARF(highdpi, 0, 1, 1, initwarning("screen resolution"));
VARF(realfullscreen, 0, 0, 1, initwarning("screen resolution"));
VAR(colorbits, 0, 0, 32);
VARF(depthbits, 0, 0, 32, initwarning("depth-buffer precision"));
VARF(stencilbits, 0, 0, 32, initwarning("stencil-buffer precision"));
VARF(fsaa, -1, -1, 16, initwarning("anti-aliasing"));
extern void updatevsync();
VARF(vsync, 0, 0, 1, updatevsync());
VARFP(vsynctear, 0, 0, 1, if(vsync) updatevsync());
XIDENTHOOK(vsynctear, IDF_EXTENDED);

void writeinitcfg()
{
    stream *f = openutf8file("init.cfg", "w");
    if(!f) return;
    f->printf("// automatically written on exit, DO NOT MODIFY\n// modify settings in game\n");
    extern int fullscreen;
    f->printf("fullscreen %d\n", fullscreen);
    f->printf("realfullscreen %d\n", realfullscreen);
    f->printf("scr_w %d\n", scr_w);
    f->printf("scr_h %d\n", scr_h);
    f->printf("highdpi %d\n", highdpi);
    f->printf("colorbits %d\n", colorbits);
    f->printf("depthbits %d\n", depthbits);
    f->printf("stencilbits %d\n", stencilbits);
    f->printf("fsaa %d\n", fsaa);
    f->printf("vsync %d\n", vsync);
    extern int useshaders, shaderprecision, forceglsl;
    f->printf("shaders %d\n", useshaders);
    f->printf("shaderprecision %d\n", shaderprecision);
    f->printf("forceglsl %d\n", forceglsl);
    extern int soundchans, soundfreq, soundbufferlen;
    f->printf("soundchans %d\n", soundchans);
    f->printf("soundfreq %d\n", soundfreq);
    f->printf("soundbufferlen %d\n", soundbufferlen);
    delete f;
}

COMMAND(quit, "");

static void getbackgroundres(int &w, int &h)
{
    float wk = 1, hk = 1;
    if(w < 1024) wk = 1024.0f/w;
    if(h < 768) hk = 768.0f/h;
    wk = hk = max(wk, hk);
    w = int(ceil(w*wk));
    h = int(ceil(h*hk));
}

string backgroundcaption = "";
Texture *backgroundmapshot = NULL;
string backgroundmapname = "";
char *backgroundmapinfo = NULL;

void restorebackground()
{
    if(renderedframe) return;
    renderbackground(backgroundcaption[0] ? backgroundcaption : NULL, backgroundmapshot, backgroundmapname[0] ? backgroundmapname : NULL, backgroundmapinfo, true);
}

void renderbackground(const char *caption, Texture *mapshot, const char *mapname, const char *mapinfo, bool restore, bool force)
{
    if(!inbetweenframes && !force) return;

    stopsounds(); // stop sounds while loading
 
    int w = screenw, h = screenh;
    if(forceaspect) w = int(ceil(h*forceaspect));
    getbackgroundres(w, h);
    gettextres(w, h);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    defaultshader->set();
    glEnable(GL_TEXTURE_2D);

    static int lastupdate = -1, lastw = -1, lasth = -1;
    static float backgroundu = 0, backgroundv = 0, detailu = 0, detailv = 0;
    static int numdecals = 0;
    static struct decal { float x, y, size; int side; } decals[12];
    if((renderedframe && !mainmenu && lastupdate != lastmillis) || lastw != w || lasth != h)
    {
        lastupdate = lastmillis;
        lastw = w;
        lasth = h;

        backgroundu = rndscale(1);
        backgroundv = rndscale(1);
        detailu = rndscale(1);
        detailv = rndscale(1);
        numdecals = sizeof(decals)/sizeof(decals[0]);
        numdecals = numdecals/3 + rnd((numdecals*2)/3 + 1);
        float maxsize = min(w, h)/16.0f;
        loopi(numdecals)
        {
            decal d = { rndscale(w), rndscale(h), maxsize/2 + rndscale(maxsize/2), rnd(2) };
            decals[i] = d;
        }
    }
    else if(lastupdate != lastmillis) lastupdate = lastmillis;

    loopi(restore ? 1 : 3)
    {
        glColor3f(1, 1, 1);
        settexture("data/background.png", 0);
        float bu = w*0.67f/256.0f + backgroundu, bv = h*0.67f/256.0f + backgroundv;
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0,  0);  glVertex2f(0, 0);
        glTexCoord2f(bu, 0);  glVertex2f(w, 0);
        glTexCoord2f(0,  bv); glVertex2f(0, h);
        glTexCoord2f(bu, bv); glVertex2f(w, h);
        glEnd();
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_BLEND);
        settexture("data/background_detail.png", 0);
        float du = w*0.8f/512.0f + detailu, dv = h*0.8f/512.0f + detailv;
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0,  0);  glVertex2f(0, 0);
        glTexCoord2f(du, 0);  glVertex2f(w, 0);
        glTexCoord2f(0,  dv); glVertex2f(0, h);
        glTexCoord2f(du, dv); glVertex2f(w, h);
        glEnd();
        settexture("data/background_decal.png", 3);
        glBegin(GL_QUADS);
        loopj(numdecals)
        {
            float hsz = decals[j].size, hx = clamp(decals[j].x, hsz, w-hsz), hy = clamp(decals[j].y, hsz, h-hsz), side = decals[j].side;
            glTexCoord2f(side,   0); glVertex2f(hx-hsz, hy-hsz);
            glTexCoord2f(1-side, 0); glVertex2f(hx+hsz, hy-hsz);
            glTexCoord2f(1-side, 1); glVertex2f(hx+hsz, hy+hsz);
            glTexCoord2f(side,   1); glVertex2f(hx-hsz, hy+hsz);
        }
        glEnd();
        float lh = 0.5f*min(w, h), lw = lh*2,
              lx = 0.5f*(w - lw), ly = 0.5f*(h*0.5f - lh);
        settexture((maxtexsize ? min(maxtexsize, hwtexsize) : hwtexsize) >= 1024 && (screenw > 1280 || screenh > 800) ? "data/logo_1024.png" : "data/logo.png", 3);
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0, 0); glVertex2f(lx,    ly);
        glTexCoord2f(1, 0); glVertex2f(lx+lw, ly);
        glTexCoord2f(0, 1); glVertex2f(lx,    ly+lh);
        glTexCoord2f(1, 1); glVertex2f(lx+lw, ly+lh);
        glEnd();

        if(caption)
        {
            int tw = text_width(caption);
            float tsz = 0.04f*min(w, h)/FONTH,
                  tx = 0.5f*(w - tw*tsz), ty = h - 0.075f*1.5f*min(w, h) - 1.25f*FONTH*tsz;
            glPushMatrix();
            glTranslatef(tx, ty, 0);
            glScalef(tsz, tsz, 1);
            draw_text(caption, 0, 0);
            glPopMatrix();
        }
        if(mapshot || mapname)
        {
            int infowidth = 12*FONTH;
            float sz = 0.35f*min(w, h), msz = (0.75f*min(w, h) - sz)/(infowidth + FONTH), x = 0.5f*(w-sz), y = ly+lh - sz/15;
            if(mapinfo)
            {
                int mw, mh;
                text_bounds(mapinfo, mw, mh, infowidth);
                x -= 0.5f*(mw*msz + FONTH*msz);
            }
            if(mapshot && mapshot!=notexture)
            {
                glBindTexture(GL_TEXTURE_2D, mapshot->id);
                glBegin(GL_TRIANGLE_STRIP);
                glTexCoord2f(0, 0); glVertex2f(x,    y);
                glTexCoord2f(1, 0); glVertex2f(x+sz, y);
                glTexCoord2f(0, 1); glVertex2f(x,    y+sz);
                glTexCoord2f(1, 1); glVertex2f(x+sz, y+sz);
                glEnd();
            }
            else
            {
                int qw, qh;
                text_bounds("?", qw, qh);
                float qsz = sz*0.5f/max(qw, qh);
                glPushMatrix();
                glTranslatef(x + 0.5f*(sz - qw*qsz), y + 0.5f*(sz - qh*qsz), 0);
                glScalef(qsz, qsz, 1);
                draw_text("?", 0, 0);
                glPopMatrix();
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            }        
            settexture("data/mapshot_frame.png", 3);
            glBegin(GL_TRIANGLE_STRIP);
            glTexCoord2f(0, 0); glVertex2f(x,    y);
            glTexCoord2f(1, 0); glVertex2f(x+sz, y);
            glTexCoord2f(0, 1); glVertex2f(x,    y+sz);
            glTexCoord2f(1, 1); glVertex2f(x+sz, y+sz);
            glEnd();
            if(mapname)
            {
                int tw = text_width(mapname);
                float tsz = sz/(8*FONTH),
                      tx = 0.9f*sz - tw*tsz, ty = 0.9f*sz - FONTH*tsz;
                if(tx < 0.1f*sz) { tsz = 0.1f*sz/tw; tx = 0.1f; }
                glPushMatrix();
                glTranslatef(x+tx, y+ty, 0);
                glScalef(tsz, tsz, 1);
                draw_text(mapname, 0, 0);
                glPopMatrix();
            }
            if(mapinfo)
            {
                glPushMatrix();
                glTranslatef(x+sz+FONTH*msz, y, 0);
                glScalef(msz, msz, 1);
                draw_text(mapinfo, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, -1, infowidth);
                glPopMatrix();
            }
        }
        glDisable(GL_BLEND);
        if(!restore) swapbuffers(false);
    }
    glDisable(GL_TEXTURE_2D);

    if(!restore)
    {
        renderedframe = false;
        copystring(backgroundcaption, caption ? caption : "");
        backgroundmapshot = mapshot;
        copystring(backgroundmapname, mapname ? mapname : "");
        if(mapinfo != backgroundmapinfo)
        {
            DELETEA(backgroundmapinfo);
            if(mapinfo) backgroundmapinfo = newstring(mapinfo);
        }
    }
}

float loadprogress = 0;

void renderprogress(float bar, const char *text, GLuint tex, bool background)   // also used during loading
{
    if(!inbetweenframes || envmapping) return;

    clientkeepalive();      // make sure our connection doesn't time out while loading maps etc.
    
    #ifdef __APPLE__
    interceptkey(SDLK_UNKNOWN); // keep the event queue awake to avoid 'beachball' cursor
    #endif

    static ullong lastprogress = 0;
    ullong now = SDL_GetTicks();
    if(now - lastprogress <= 1000/59) return;
    lastprogress = now;

    extern int sdl_backingstore_bug;
    if(background || sdl_backingstore_bug > 0) restorebackground();

    int w = screenw, h = screenh;
    if(forceaspect) w = int(ceil(h*forceaspect));
    getbackgroundres(w, h);
    gettextres(w, h);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, w, h, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glEnable(GL_TEXTURE_2D);
    defaultshader->set();
    glColor3f(1, 1, 1);

    float fh = 0.075f*min(w, h), fw = fh*10,
          fx = renderedframe ? w - fw - fh/4 : 0.5f*(w - fw), 
          fy = renderedframe ? fh/4 : h - fh*1.5f,
          fu1 = 0/512.0f, fu2 = 511/512.0f,
          fv1 = 0/64.0f, fv2 = 52/64.0f;
    settexture("data/loading_frame.png", 3);
    glBegin(GL_TRIANGLE_STRIP);
    glTexCoord2f(fu1, fv1); glVertex2f(fx,    fy);
    glTexCoord2f(fu2, fv1); glVertex2f(fx+fw, fy);
    glTexCoord2f(fu1, fv2); glVertex2f(fx,    fy+fh);
    glTexCoord2f(fu2, fv2); glVertex2f(fx+fw, fy+fh);
    glEnd();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float bw = fw*(511 - 2*17)/511.0f, bh = fh*20/52.0f,
          bx = fx + fw*17/511.0f, by = fy + fh*16/52.0f,
          bv1 = 0/32.0f, bv2 = 20/32.0f,
          su1 = 0/32.0f, su2 = 7/32.0f, sw = fw*7/511.0f,
          eu1 = 23/32.0f, eu2 = 30/32.0f, ew = fw*7/511.0f,
          mw = bw - sw - ew,
          ex = bx+sw + max(mw*bar, fw*7/511.0f);
    if(bar > 0)
    {
        settexture("data/loading_bar.png", 3);
        glBegin(GL_QUADS);
        glTexCoord2f(su1, bv1); glVertex2f(bx,    by);
        glTexCoord2f(su2, bv1); glVertex2f(bx+sw, by);
        glTexCoord2f(su2, bv2); glVertex2f(bx+sw, by+bh);
        glTexCoord2f(su1, bv2); glVertex2f(bx,    by+bh);

        glTexCoord2f(su2, bv1); glVertex2f(bx+sw, by);
        glTexCoord2f(eu1, bv1); glVertex2f(ex,    by);
        glTexCoord2f(eu1, bv2); glVertex2f(ex,    by+bh);
        glTexCoord2f(su2, bv2); glVertex2f(bx+sw, by+bh);

        glTexCoord2f(eu1, bv1); glVertex2f(ex,    by);
        glTexCoord2f(eu2, bv1); glVertex2f(ex+ew, by);
        glTexCoord2f(eu2, bv2); glVertex2f(ex+ew, by+bh);
        glTexCoord2f(eu1, bv2); glVertex2f(ex,    by+bh);
        glEnd();
    }

    if(text)
    {
        int tw = text_width(text);
        float tsz = bh*0.8f/FONTH;
        if(tw*tsz > mw) tsz = mw/tw;
        glPushMatrix();
        glTranslatef(bx+sw, by + (bh - FONTH*tsz)/2, 0);
        glScalef(tsz, tsz, 1);
        draw_text(text, 0, 0);
        glPopMatrix();
    }

    glDisable(GL_BLEND);

    if(tex)
    {
        glBindTexture(GL_TEXTURE_2D, tex);
        float sz = 0.35f*min(w, h), x = 0.5f*(w-sz), y = 0.5f*min(w, h) - sz/15;
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0, 0); glVertex2f(x,    y);
        glTexCoord2f(1, 0); glVertex2f(x+sz, y);
        glTexCoord2f(0, 1); glVertex2f(x,    y+sz);
        glTexCoord2f(1, 1); glVertex2f(x+sz, y+sz);
        glEnd();

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        settexture("data/mapshot_frame.png", 3);
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0, 0); glVertex2f(x,    y);
        glTexCoord2f(1, 0); glVertex2f(x+sz, y);
        glTexCoord2f(0, 1); glVertex2f(x,    y+sz);
        glTexCoord2f(1, 1); glVertex2f(x+sz, y+sz);
        glEnd();
        glDisable(GL_BLEND);
    }

    glDisable(GL_TEXTURE_2D);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    swapbuffers(false);
}

int selectedcontrollernum = -1;
int ngamecontrollers = 0;
SDL_GameController **gamecontrollers = NULL;
const int maxhaptics = 10;
int nhaptics = 0;
SDL_Haptic **haptics = NULL;

SVARP(gamecontroller, "");
XIDENTHOOK(gamecontroller, IDF_EXTENDED);

void initgamecontrollers(bool enable) {
    if(ngamecontrollers && gamecontrollers) {
        loopi(ngamecontrollers) {
            if(gamecontrollers[i])
                SDL_GameControllerClose(gamecontrollers[i]);
        }
        ngamecontrollers = 0;
        delete[] gamecontrollers;
        gamecontrollers = NULL;
    }
    selectedcontrollernum = -1;
    if(enable) {
        char guid_str[1024];
        ngamecontrollers = SDL_NumJoysticks();
        if(ngamecontrollers <= 0) return;
        SDL_RWops* bindings = SDL_RWFromConstMem(gamepadmappingsdb, strlen(gamepadmappingsdb));
        SDL_GameControllerAddMappingsFromRW(bindings, 1);
        gamecontrollers = new SDL_GameController*[ngamecontrollers];
        loopi(ngamecontrollers) {
            gamecontrollers[i] = SDL_GameControllerOpen(i);
            if(gamecontrollers[i]) {
                SDL_Joystick* j = SDL_GameControllerGetJoystick(gamecontrollers[i]);
                SDL_JoystickGUID guid = SDL_JoystickGetGUID(j);
                SDL_JoystickGetGUIDString(guid, guid_str, sizeof(guid_str));
                if(!strcmp(gamecontroller, guid_str)) {
                    selectedcontrollernum = i;
                    conoutf("selected gamepad#%d: %s", i, SDL_GameControllerName(gamecontrollers[i]));
                } else {
                    conoutf("gamepad#%d: %s", i, SDL_GameControllerName(gamecontrollers[i]));
                }
            }
        }
        if(ngamecontrollers > 0 && selectedcontrollernum < 0) {
            if(gamecontrollers[0]) {
                conoutf("Setting '%s' as the main gamepad",  SDL_GameControllerName(gamecontrollers[0]));
                selectedcontrollernum = 0;
                SDL_Joystick* j = SDL_GameControllerGetJoystick(gamecontrollers[0]);
                SDL_JoystickGUID guid = SDL_JoystickGetGUID(j);
                SDL_JoystickGetGUIDString(guid, guid_str, sizeof(guid_str));
                conoutf(guid_str);
                setsvar("gamecontroller", guid_str);
            }
        }
    }
}

VARP(haptic0Mode, 0, 0, 3);
XIDENTHOOK(haptic0Mode, IDF_EXTENDED);
VARP(haptic0Cap, 0, 50, 100);
XIDENTHOOK(haptic0Cap, IDF_EXTENDED);

VARP(haptic1Mode, 0, 0, 3);
XIDENTHOOK(haptic1Mode, IDF_EXTENDED);
VARP(haptic1Cap, 0, 50, 100);
XIDENTHOOK(haptic1Cap, IDF_EXTENDED);

VARP(haptic2Mode, 0, 0, 3);
XIDENTHOOK(haptic2Mode, IDF_EXTENDED);
VARP(haptic2Cap, 0, 50, 100);
XIDENTHOOK(haptic2Cap, IDF_EXTENDED);

VARP(haptic3Mode, 0, 0, 3);
XIDENTHOOK(haptic3Mode, IDF_EXTENDED);
VARP(haptic3Cap, 0, 50, 100);
XIDENTHOOK(haptic3Cap, IDF_EXTENDED);

VARP(haptic4Mode, 0, 0, 3);
XIDENTHOOK(haptic4Mode, IDF_EXTENDED);
VARP(haptic4Cap, 0, 50, 100);
XIDENTHOOK(haptic4Cap, IDF_EXTENDED);

VARP(haptic5Mode, 0, 0, 3);
XIDENTHOOK(haptic5Mode, IDF_EXTENDED);
VARP(haptic5Cap, 0, 50, 100);
XIDENTHOOK(haptic5Cap, IDF_EXTENDED);

VARP(haptic6Mode, 0, 0, 3);
XIDENTHOOK(haptic6Mode, IDF_EXTENDED);
VARP(haptic6Cap, 0, 50, 100);
XIDENTHOOK(haptic6Cap, IDF_EXTENDED);

VARP(haptic7Mode, 0, 0, 3);
XIDENTHOOK(haptic7Mode, IDF_EXTENDED);
VARP(haptic7Cap, 0, 50, 100);
XIDENTHOOK(haptic7Cap, IDF_EXTENDED);

VARP(haptic8Mode, 0, 0, 3);
XIDENTHOOK(haptic8Mode, IDF_EXTENDED);
VARP(haptic8Cap, 0, 50, 100);
XIDENTHOOK(haptic8Cap, IDF_EXTENDED);

VARP(haptic9Mode, 0, 0, 3);
XIDENTHOOK(haptic9Mode, IDF_EXTENDED);
VARP(haptic9Cap, 0, 50, 100);
XIDENTHOOK(haptic9Cap, IDF_EXTENDED);


int gethapticcap(int num) {
    #define MAXPATLEN 200
    static char cmdbuff[MAXPATLEN];
    snprintf(cmdbuff, MAXPATLEN, "$haptic%dCap", num);
    const char* result = executestr(cmdbuff);
    if(!result) return 0;
    int v = parseint(result);
    DELETEA(result);
    return v;
}

int gethapticmode(int num) {
    #define MAXPATLEN 200
    static char cmdbuff[MAXPATLEN];
    snprintf(cmdbuff, MAXPATLEN, "$haptic%dMode", num);
    const char* result = executestr(cmdbuff);
    if(!result) return 0;
    int v = parseint(result);
    DELETEA(result);
    return v;
}

void rumblehaptics(int mode, int power, int duration) {
    power = clamp(power, 0, 100);
    duration = clamp(duration, 0, 1000);
    loopi(nhaptics) {
        if(gethapticmode(i) & mode) {
            SDL_HapticRumblePlay(haptics[i], power * gethapticcap(i)/10000.0, duration);
        }
    }
}

void inithaptics(bool enable) {
   if(nhaptics && haptics) {
        loopi(nhaptics) {
            if(haptics[i])
                SDL_HapticClose(haptics[i]);
        }
        nhaptics = 0;
        delete[] haptics;
        haptics = NULL;
    }
    if(enable) {
        nhaptics = SDL_NumHaptics();
        if(nhaptics <= 0) return;
        nhaptics = clamp(nhaptics, 0, maxhaptics);
        haptics = new SDL_Haptic*[nhaptics];
        loopi(nhaptics) {
            haptics[i] = SDL_HapticOpen(i);
            if(haptics[i]) {
                SDL_HapticRumbleInit(haptics[i]);
            }
        }
        loopi(nhaptics) {
            conoutf("haptic#%d: %s, mode: %d, cap: %d", i
                    , SDL_HapticName(i), gethapticmode(i), gethapticcap(i));
            if(gethapticmode(i))
                SDL_HapticRumblePlay(haptics[i], gethapticcap(i)/100.0, 1000);
        }
    }
}

void reconfigrecontrollers();

VARFP(gamepad, 0, 0, 1, reconfigrecontrollers());
XIDENTHOOK(gamepad, IDF_EXTENDED);

VARFP(haptic, 0, 0, 1, reconfigrecontrollers());
XIDENTHOOK(haptic, IDF_EXTENDED);

void reconfigrecontrollers() {
    if(initing < INIT_LOAD) {
        initgamecontrollers(gamepad);
        inithaptics(haptic);
    }
}

VARNP(relativemouse, userelativemouse, 0, 1, 1);
XIDENTHOOK(relativemouse, IDF_EXTENDED);

bool grabinput = false, minimized = false, canrelativemouse = true, relativemouse = false, isentered = false, isfocused = false, shouldminimize = false;
int keyrepeatmask = 0, textinputmask = 0;

void keyrepeat(bool on, int mask)
{
    if(on) keyrepeatmask |= mask;
    else keyrepeatmask &= ~mask;
}

void textinput(bool on, int mask)
{
    if(on) 
    {
        if(!textinputmask) SDL_StartTextInput(); 
        textinputmask |= mask;
    }
    else
    {
        textinputmask &= ~mask;
        if(!textinputmask) SDL_StopTextInput();
    }
}

void inputgrab(bool on)
{
    if(on)
    {
        SDL_ShowCursor(SDL_FALSE);
        if(canrelativemouse && userelativemouse)
        {
            if(SDL_SetRelativeMouseMode(SDL_TRUE) >= 0) 
            {
                SDL_SetWindowGrab(screen, SDL_TRUE);
                relativemouse = true;
            }
            else 
            {
                SDL_SetWindowGrab(screen, SDL_TRUE);
                canrelativemouse = false;
                relativemouse = false;
                conoutf(CON_WARN, "Unable to use SDL2 relative mouse input");
            }
        }
    }
    else
    {
        SDL_ShowCursor(SDL_TRUE);
        if(relativemouse)
        {
            SDL_SetRelativeMouseMode(SDL_FALSE);
            relativemouse = false;
        }
        SDL_SetWindowGrab(screen, SDL_FALSE);
    }
}

void inputhandling(bool on) {
    if(on) {
        SDL_EventState(SDL_MOUSEMOTION, SDL_ENABLE);
        SDL_EventState(SDL_MOUSEWHEEL, SDL_ENABLE);
        SDL_EventState(SDL_MOUSEBUTTONDOWN, SDL_ENABLE);
        SDL_EventState(SDL_MOUSEBUTTONUP, SDL_ENABLE);
        inputgrab(grabinput = true);
    } else {
        SDL_EventState(SDL_MOUSEMOTION, SDL_IGNORE);
        SDL_EventState(SDL_MOUSEWHEEL, SDL_IGNORE);
        SDL_EventState(SDL_MOUSEBUTTONDOWN, SDL_IGNORE);
        SDL_EventState(SDL_MOUSEBUTTONUP, SDL_IGNORE);
        inputgrab(grabinput = false);
    }
}

bool initwindowpos = false;

void setfullscreen(bool enable)
{
    if(!screen) return;
    //initwarning(enable ? "fullscreen" : "windowed");
    int sflags = realfullscreen ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_SetWindowFullscreen(screen, enable ? sflags : 0);
    if(!enable) 
    {
        SDL_SetWindowSize(screen, scr_w, scr_h);
        if(initwindowpos)
        {
            int winx = SDL_WINDOWPOS_CENTERED, winy = SDL_WINDOWPOS_CENTERED;
            SDL_SetWindowPosition(screen, winx, winy);
            initwindowpos = false;
        }
    }
}

VARF(fullscreen, 0, 1, 1, setfullscreen(fullscreen!=0));

void screenres(int w, int h)
{
    scr_w = clamp(w, SCR_MINW, SCR_MAXW);
    scr_h = clamp(h, SCR_MINH, SCR_MAXH);
    if(screen)
    {
        scr_w = min(scr_w, desktopw);
        scr_h = min(scr_h, desktoph);
        if(SDL_GetWindowFlags(screen) & SDL_WINDOW_FULLSCREEN) gl_resize();
        else SDL_SetWindowSize(screen, scr_w, scr_h);
    }
    else 
    {
        initwarning("screen resolution");
    }
}

ICOMMAND(screenres, "ii", (int *w, int *h), screenres(*w, *h));

static int curgamma = 100;
VARFP(gamma, 30, 100, 300,
{
    if(gamma == curgamma) return;
    curgamma = gamma;
    if(SDL_SetWindowBrightness(screen, gamma/100.0f)==-1) conoutf(CON_ERROR, "Could not set gamma: %s", SDL_GetError());
});

void restoregamma()
{
    if(curgamma == 100) return;
    SDL_SetWindowBrightness(screen, curgamma/100.0f);
}

void cleargamma()
{
    if(curgamma != 100 && screen) SDL_SetWindowBrightness(screen, 1.0f);
}

VAR(dbgmodes, 0, 0, 1);

VARP(tearfree_method, 0, 0, 2);
XIDENTHOOK(tearfree_method, IDF_EXTENDED);

struct SyncWindow
{
    int isconstructed;
    int isbroken;
    int synctype;
    ullong lasttimestamp;
    static const size_t historysize = 10;
    ullong* timestamphistory;
    SDL_mutex *stamp_mutex;
    SDL_Thread *thread;
    int killthread;
    SDL_Window *window;
    SDL_GLContext glcontext;
    SyncWindow();
    ~SyncWindow();
    ullong getlastsyncstamp();
    uint getsyncinterval();
};


ullong SyncWindow::getlastsyncstamp()
{
    ullong stamp;
    SDL_LockMutex(stamp_mutex);
    stamp = lasttimestamp;
    SDL_UnlockMutex(stamp_mutex);
    return stamp;
}

uint SyncWindow::getsyncinterval()
{
    ullong interval = 0;
    SDL_LockMutex(stamp_mutex);
    loopi(historysize-1) {
        if(timestamphistory[i+1]) interval += timestamphistory[i] - timestamphistory[i+1];
    }
    SDL_UnlockMutex(stamp_mutex);
    interval/=historysize-1;
    if(interval > 33333333U || ! interval) return 33333333U;
    return static_cast<uint>(interval);
}

void precisenanosleep(ullong nsec);
static inline void sleepwrapper(llong sec, llong nsec);
static inline ullong tick_nsec();


VAR(debugsyncthreadinfo, 0, 1, 1);
typedef int (APIENTRY * glXGetVideoSyncSGI_fn)(uint *count);
typedef int (APIENTRY * glXWaitVideoSyncSGI_fn)(int divisor,
                                                int remainder,
                                                uint *count);

int syncwindow_threadfn(void *data)
{
    ullong timestamp;
    SyncWindow *obj = static_cast<SyncWindow*>(data);
    int synctype = obj->synctype;
    glXGetVideoSyncSGI_fn getsyncstamp = NULL;
    glXWaitVideoSyncSGI_fn waitforsync = NULL;
    int i = 0;

    if(synctype == 1 || synctype == 2) {
        if(synctype == 1 || synctype == 2) {
            obj->window = SDL_CreateWindow("Sync Window", 0, 0, 100, 100, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
            SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
            obj->glcontext = SDL_GL_CreateContext(obj->window);
            glFinish();
        }
        while(SDL_GL_MakeCurrent(obj->window, obj->glcontext)) {
            sleepwrapper(0,250000000);
            i++;
            if(debugsyncthreadinfo) conoutf("Setting GL context failed (attempt %d), error: %s", i, SDL_GetError());
            if(i >= 20) {
                conoutf("Setting GL context error, tearfree is disabled");
                obj->isbroken++;
                return -1;
            }
        }
        if(i && debugsyncthreadinfo) {
            conoutf("Setting GL context success (attempt %d)", i+1);
        }
        i = 0;
    }

    bool is_init = false;
    if(synctype == 2) {
        SDL_GL_SetSwapInterval(1);
    }

    uint sync_counter = 0;
    if(synctype == 1) {
        getsyncstamp = (glXGetVideoSyncSGI_fn)SDL_GL_GetProcAddress("glXGetVideoSyncSGI");
        waitforsync = (glXWaitVideoSyncSGI_fn)SDL_GL_GetProcAddress("glXWaitVideoSyncSGI");
        if(!getsyncstamp || !waitforsync) {
            conoutf("Openg GL sync extension initialisation error, tearfree is disabled");
            obj->isbroken++;
            return -1;
        }
        int err = getsyncstamp(&sync_counter);
        if(err) {
            conoutf("Openg GL timestamp error, tearfree is disabled");
            obj->isbroken++;
            return -1;
        }
        is_init = true;
    }

    while(true) {

        if(obj->killthread) {
            if(synctype == 1 || synctype == 2) {
                SDL_GL_DeleteContext(obj->glcontext);
                SDL_DestroyWindow(obj->window);
            }
            return 0;
        }

        if(synctype == 2) {
            if(!is_init) {
                if(SDL_GL_GetCurrentWindow() != obj->window || SDL_GL_GetCurrentContext() != obj->glcontext || ! SDL_GL_GetSwapInterval()) {
                    i++;
                    if(debugsyncthreadinfo) conoutf("GL context params mismatch (attempt %d)", i);
                    if(i > 20) {
                        conoutf("Setting GL params failed, tearfree is disabled");
                        obj->isbroken++;
                        return -1;
                    }
                    SDL_GL_MakeCurrent(obj->window, obj->glcontext);
                    SDL_GL_SetSwapInterval(1);
                    sleepwrapper(0, 250000000);
                    continue;
                } else {
                    if(i && debugsyncthreadinfo) conoutf("GL context params are set (attempt %d)", i+1);
                    is_init = true;
                }
            }
            SDL_GL_SwapWindow(obj->window);
            glFinish();
        }

        if(synctype == 1) {
            int t = (sync_counter + 1) % 2;
            waitforsync(2, t, &sync_counter);
        }

        timestamp = tick_nsec();

        SDL_LockMutex(obj->stamp_mutex);
        obj->lasttimestamp = timestamp;
        memmove(obj->timestamphistory + 1, obj->timestamphistory, (obj->historysize-1)*sizeof(ullong));
        obj->timestamphistory[0] = timestamp;
        SDL_UnlockMutex(obj->stamp_mutex);
    }
}


SyncWindow::SyncWindow()
{
    isconstructed = 0;
    isbroken = 0;
    lasttimestamp = 0;
    stamp_mutex = SDL_CreateMutex();
    killthread = 0;
    synctype = 0;
    timestamphistory = new ullong[historysize];
    loopi(historysize) {
        timestamphistory[i] = 0;
    }
    if(!tearfree_method) {
        synctype = 1;
    } else {
        synctype = tearfree_method;
    }
    isconstructed = 1;
    thread = SDL_CreateThread(syncwindow_threadfn, "SyncWindow Thread", this);
}

SyncWindow::~SyncWindow()
{
    int code;
    killthread++;
    if(isconstructed) {
        SDL_WaitThread(thread, &code);
    }
    delete[] timestamphistory;
    SDL_DestroyMutex(stamp_mutex);
}

SyncWindow *syncwin;

bool checksyncwin() {
    if(!syncwin) return false;
    if(!syncwin->isconstructed || syncwin->isbroken) return false;
    return true;
}

void setupscreen(int &useddepthbits, int &usedfsaa)
{
    if(glcontext)
    {
        SDL_GL_DeleteContext(glcontext);
        glcontext = NULL;
    }
    if(screen)
    {
        SDL_DestroyWindow(screen);
        screen = NULL;
    }

    SDL_DisplayMode desktop;
    if(SDL_GetDesktopDisplayMode(0, &desktop) < 0) fatal("failed querying desktop display mode: %s", SDL_GetError());
    desktopw = desktop.w;
    desktoph = desktop.h;

    if(scr_h < 0) scr_h = SCR_DEFAULTH;
    if(scr_w < 0) scr_w = (scr_h*desktopw)/desktoph;
    scr_w = min(scr_w, desktopw);
    scr_h = min(scr_h, desktoph);

    int winx = SDL_WINDOWPOS_UNDEFINED, winy = SDL_WINDOWPOS_UNDEFINED, winw = scr_w, winh = scr_h, flags = SDL_WINDOW_RESIZABLE;
    if(highdpi) flags |= SDL_WINDOW_ALLOW_HIGHDPI;
    if(fullscreen)
    {
        winw = desktopw;
        winh = desktoph;
        flags |= realfullscreen ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_FULLSCREEN_DESKTOP;
        initwindowpos = true;
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    static int configs[] =
    {
        0x7, /* try everything */
        0x6, 0x5, 0x3, /* try disabling one at a time */
        0x4, 0x2, 0x1, /* try disabling two at a time */
        0 /* try disabling everything */
    };
    int config = 0;
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    if(!depthbits) SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    if(!fsaa)
    {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
    }
    loopi(sizeof(configs)/sizeof(configs[0]))
    {
        config = configs[i];
        if(!depthbits && config&1) continue;
        if(!stencilbits && config&2) continue;
        if(fsaa<=0 && config&4) continue;
        if(depthbits) SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, config&1 ? depthbits : 16);
        if(stencilbits)
        {
            hasstencil = config&2 ? stencilbits : 0;
            SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, hasstencil);
        }
        else hasstencil = 0;
        if(fsaa>0)
        {
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, config&4 ? 1 : 0);
            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, config&4 ? fsaa : 0);
        }
        screen = SDL_CreateWindow("Cube 2: Sauerbraten", winx, winy, winw, winh, SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS | flags);
        if(screen) break;
    }
    if(!screen) fatal("failed to create OpenGL window: %s", SDL_GetError());
    else
    {
        if(depthbits && (config&1)==0) conoutf(CON_WARN, "%d bit z-buffer not supported - disabling", depthbits);
        if(stencilbits && (config&2)==0) conoutf(CON_WARN, "Stencil buffer not supported - disabling");
        if(fsaa>0 && (config&4)==0) conoutf(CON_WARN, "%dx anti-aliasing not supported - disabling", fsaa);
    }

    SDL_SetWindowMinimumSize(screen, SCR_MINW, SCR_MINH);
    SDL_SetWindowMaximumSize(screen, SCR_MAXW, SCR_MAXH);

    glcontext = SDL_GL_CreateContext(screen);
    if(!glcontext) fatal("failed to create OpenGL context: %s", SDL_GetError());
    updatevsync();

    SDL_GetWindowSize(screen, &screenw, &screenh);

    useddepthbits = config&1 ? depthbits : 0;
    usedfsaa = config&4 ? fsaa : 0;
}

void updatevsync(){
       if(!glcontext) return;
       if(!SDL_GL_SetSwapInterval(vsync ? (vsynctear ? -1 : 1) : 0)) return;
       if(vsync && vsynctear) conoutf("vsynctear not supported, or you need to restart sauer to apply changes.");
       else if(vsync) conoutf("vsynctear not supported, or you need to restart sauer to apply changes.");
       else conoutf("You need to restart sauer to disable vsync.");
}

void resetgl()
{
    clearchanges(CHANGE_GFX);

    renderbackground("resetting OpenGL");

    extern void cleanupva();
    extern void cleanupparticles();
    extern void cleanupsky();
    extern void cleanupmodels();
    extern void cleanuptextures();
    extern void cleanuplightmaps();
    extern void cleanupblendmap();
    extern void cleanshadowmap();
    extern void cleanreflections();
    extern void cleanupglare();
    extern void cleanupdepthfx();
    extern void cleanupshaders();
    extern void cleanupgl();
    recorder::cleanup();
    cleanupva();
    cleanupparticles();
    cleanupsky();
    cleanupmodels();
    cleanuptextures();
    cleanuplightmaps();
    cleanupblendmap();
    cleanshadowmap();
    cleanreflections();
    cleanupglare();
    cleanupdepthfx();
    cleanupshaders();
    cleanupgl();
    
    int useddepthbits = 0, usedfsaa = 0;
    setupscreen(useddepthbits, usedfsaa);

    inputgrab(grabinput);

    gl_init(useddepthbits, usedfsaa);

    extern void reloadfonts();
    extern void reloadtextures();
    extern void reloadshaders();
    inbetweenframes = false;
    if(!reloadtexture(*notexture) ||
       !reloadtexture("data/logo.png") ||
       !reloadtexture("data/logo_1024.png") || 
       !reloadtexture("data/background.png") ||
       !reloadtexture("data/background_detail.png") ||
       !reloadtexture("data/background_decal.png") ||
       !reloadtexture("data/mapshot_frame.png") ||
       !reloadtexture("data/loading_frame.png") ||
       !reloadtexture("data/loading_bar.png"))
        fatal("failed to reload core texture");
    reloadfonts();
    inbetweenframes = true;
    renderbackground("initializing...");
	restoregamma();
    reloadshaders();
    reloadtextures();
    initlights();
    allchanged(true);
}

COMMAND(resetgl, "");

vector<SDL_Event> events;

void pushevent(const SDL_Event &e)
{
    events.add(e); 
}

static bool filterevent(const SDL_Event &event)
{
    switch(event.type)
    {
        case SDL_MOUSEMOTION:
            if(grabinput && !relativemouse && !(SDL_GetWindowFlags(screen) & SDL_WINDOW_FULLSCREEN))
            {
                if(event.motion.x == screenw / 2 && event.motion.y == screenh / 2) 
                    return false;  // ignore any motion events generated by SDL_WarpMouse
            }
            break;
    }
    return true;
}

static inline bool pollevent(SDL_Event &event)
{
    while(SDL_PollEvent(&event))
    {
        if(filterevent(event)) return true;
    }
    return false;
}

bool interceptkey(int sym)
{
    static int lastintercept = SDLK_UNKNOWN;
    int len = lastintercept == sym ? events.length() : 0;
    SDL_Event event;
    while(pollevent(event))
    {
        switch(event.type)
        {
            case SDL_MOUSEMOTION: break;
            default: pushevent(event); break;
        }
    }
    lastintercept = sym;
    if(sym != SDLK_UNKNOWN) for(int i = len; i < events.length(); i++)
    {
        if(events[i].type == SDL_KEYDOWN && events[i].key.keysym.sym == sym) { events.remove(i); return true; }
    }
    return false;
}

static void ignoremousemotion()
{
    SDL_Event e;
    SDL_PumpEvents();
    while(SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_MOUSEMOTION, SDL_MOUSEMOTION));
}

static void resetmousemotion()
{
    if(grabinput && !relativemouse && !(SDL_GetWindowFlags(screen) & SDL_WINDOW_FULLSCREEN))
    {
        SDL_WarpMouseInWindow(screen, screenw / 2, screenh / 2);
    }
}

static void checkmousemotion(int &dx, int &dy)
{
    loopv(events)
    {
        SDL_Event &event = events[i];
        if(event.type != SDL_MOUSEMOTION)
        { 
            if(i > 0) events.remove(0, i); 
            return; 
        }
        dx += event.motion.xrel;
        dy += event.motion.yrel;
    }
    events.setsize(0);
    SDL_Event event;
    while(pollevent(event))
    {
        if(event.type != SDL_MOUSEMOTION)
        {
            events.add(event);
            return;
        }
        dx += event.motion.xrel;
        dy += event.motion.yrel;
    }
}

void checkunfocused(SDL_Event event) {
    static bool hasalt = false;

    if(event.key.state != SDL_PRESSED) {
        hasalt = false;
        return;
    }

    if(event.key.keysym.sym == SDLK_LALT || event.key.keysym.sym == SDLK_RALT) {
        hasalt = true;
        return;
    }

    if(hasalt && event.key.keysym.sym == SDLK_RETURN) {
        shouldminimize = true;
        return;
    }

    hasalt = false;
}

VARP(stickdeadzone, 0, 20, 40);
XIDENTHOOK(stickdeadzone, IDF_EXTENDED);

VARP(triggerlevel, 10, 75, 100);
XIDENTHOOK(triggerlevel, IDF_EXTENDED);

const int maxstickval = 32767;

struct TriggerInfo {
    hashset<const char*> activetriggers;
    void add(const char* name) {
        activetriggers[name] = name;
    }
    void remove(const char* name) {
        activetriggers.remove(name);
    }
    bool isactive(const char* name) {
        const char* c = activetriggers.find(name, NULL);
        return c ? true : false;
    }
} triggerinfo;


int gettriggerdz() {
    return maxstickval * (triggerlevel/100.0);
}

struct AxisInfo {
    hashtable<const char*, int> axisvalues;
    hashset<const char*> activeaxis;
    hashtable<const char*, const char*> pairs;
    AxisInfo() {
        pairs["rightx"] = "righty";
        pairs["righty"] = "rightx";
        pairs["lefty"] = "leftx";
        pairs["leftx"] = "lefty";
    }
    void add(const char* name, int val) {
        axisvalues[name] = val;
    }
    int getvalue(const char* name) {
        int v = axisvalues.find(name, 0);
        if(!v) return 0;
        return v; 
    }
    const char* findpair(const char* name) {
        return pairs.find(name, NULL);
    }
    int getpairvalue(const char* name) {
        const char* pairname = pairs.find(name, NULL);
        if(!pairname) return 0;
        return getvalue(pairname);
    }
    void setactive(const char* name) {
        const char* pair = findpair(name);
        activeaxis[name] = name;
        activeaxis[pair] = pair;
    }
    void setinactive(const char* name) {
        const char* pair = findpair(name);
        activeaxis.remove(name);
        activeaxis.remove(pair);
    }
    bool isactive(const char* name) {
        const char* pair = findpair(name);
        const char* c1 = activeaxis.find(name, NULL);
        const char* c2 = activeaxis.find(pair, NULL);
        return c1 && c2;
    }
} axisinfo;

const char* getaxisname(SDL_ControllerAxisEvent caxis) {
    return SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)caxis.axis);
}

const char* getbuttonname(SDL_ControllerButtonEvent cbutton) {
    return SDL_GameControllerGetStringForButton((SDL_GameControllerButton)cbutton.button);
}

int getstickdzmagnitude() {
    return maxstickval * (stickdeadzone/100.0);
}

bool isdzactive(const char* name) {
    long v = abs(axisinfo.getvalue(name));
    long vp = abs(axisinfo.getpairvalue(name));
    int dz = getstickdzmagnitude();
    long l = sqrt(pow(v,2) + pow(vp,2));
    return l < dz;
}

double clampedstickvel(const char* name) {
    int v = axisinfo.getvalue(name);
    long vp = axisinfo.getpairvalue(name);
    int dz = getstickdzmagnitude();
    long l = sqrt(pow(v,2) + pow(vp,2));
    long la = l - dz;
    if(la <= 0.0) return 0.0;
    long vd = dz*abs(v)/l;
    double activeval = (double)(v < 0 ? v+vd : v-vd);
    double activescale = (double)(maxstickval-dz);
    double result = clamp(activeval/activescale, -1.0, 1.0);
    return result; 
}

bool istrigger(SDL_ControllerAxisEvent caxis) {
    const char* axisname = getaxisname(caxis);
    if(!strcmp(axisname, "lefttrigger") || !strcmp(axisname, "righttrigger")) {
        return true;
    }
    return false;
}

void caxismove(SDL_ControllerAxisEvent caxis) {
    if(caxis.which != selectedcontrollernum) return;
    int val = caxis.value;
    const char* name = getaxisname(caxis);
    if(istrigger(caxis)) {
        int dz = gettriggerdz();
        bool active = triggerinfo.isactive(name);
        if(val > dz && !active) {
            triggerinfo.add(name);
            conoutf("Button %s", name);
        } else if (active && val < dz) {
            triggerinfo.remove(name);
        }
    } else {
        axisinfo.add(name, val);
        bool activedz = isdzactive(name);
        if(!activedz) {
            axisinfo.setactive(name);
            conoutf("Stick %s: %lf", name, clampedstickvel(name));
        } else if(axisinfo.isactive(name)) {
            axisinfo.setinactive(name);
            conoutf("Stict stop %s %s", name, axisinfo.findpair(name));
        }
    }
}

void cbuttonevent(SDL_ControllerButtonEvent cbutton) {
    if(cbutton.which != selectedcontrollernum) return;
    if(!isfocused) return;
    if(cbutton.state == SDL_RELEASED) {
        conoutf("Button released %s", getbuttonname(cbutton));
    } else {
        conoutf("Button pressed %s", getbuttonname(cbutton));
    }
}

void checkinput()
{
    SDL_Event event;
    //int lasttype = 0, lastbut = 0;
    bool mousemoved = false; 
    while(events.length() || pollevent(event))
    {
        if(events.length()) event = events.remove(0);

        switch(event.type)
        {
            case SDL_QUIT:
                quit();
                return;

            case SDL_TEXTINPUT:
            {
                static uchar buf[SDL_TEXTINPUTEVENT_TEXT_SIZE+1];
                int len = decodeutf8(buf, int(sizeof(buf)-1), (const uchar *)event.text.text, strlen(event.text.text));
                if(len > 0) { buf[len] = '\0'; processtextinput((const char *)buf, len); }
                break;
            }

            case SDL_KEYDOWN:
            case SDL_KEYUP:
                checkunfocused(event);
                if(keyrepeatmask || !event.key.repeat)
                    processkey(event.key.keysym.sym, event.key.state==SDL_PRESSED);
                break;

            case SDL_WINDOWEVENT:
                switch(event.window.event)
                {
                    case SDL_WINDOWEVENT_CLOSE:
                        quit();
                        break;

                    case SDL_WINDOWEVENT_ENTER:
                        isentered = true;
                        if(isentered && isfocused) inputhandling(true);
                        break;

                    case SDL_WINDOWEVENT_LEAVE:
                        isentered = false;
                        break;

                    case SDL_WINDOWEVENT_FOCUS_GAINED:
                        isfocused = true;
                        if(isentered && isfocused) inputhandling(true);
                        break;

                    case SDL_WINDOWEVENT_FOCUS_LOST:
                        isfocused = false;
                        inputhandling(false);
                        break;

                    case SDL_WINDOWEVENT_MINIMIZED:
                        minimized = true;
                        break;

                    case SDL_WINDOWEVENT_MAXIMIZED:
                    case SDL_WINDOWEVENT_RESTORED:
                        minimized = false;
                        break;

                    case SDL_WINDOWEVENT_RESIZED:
                        break;

                    case SDL_WINDOWEVENT_SIZE_CHANGED:
                    {
                        SDL_GetWindowSize(screen, &screenw, &screenh);
                        if(!(SDL_GetWindowFlags(screen) & SDL_WINDOW_FULLSCREEN))
                        {
                            scr_w = clamp(screenw, SCR_MINW, SCR_MAXW);
                            scr_h = clamp(screenh, SCR_MINH, SCR_MAXH);
                        }
                        gl_resize();
                    }
                        break;
                }
                break;

            case SDL_MOUSEMOTION:
                if(grabinput)
                {
                    int dx = event.motion.xrel, dy = event.motion.yrel;
                    checkmousemotion(dx, dy);
                    if(!g3d_movecursor(dx, dy)) mousemove(dx, dy);
                    mousemoved = true;
                }
                break;

            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
#ifdef __linux__
#define keycodeshift 0
#else
#define keycodeshift 2*(event.button.button>=SDL_BUTTON_X1)
#endif
                processkey(-event.button.button - keycodeshift, event.button.state==SDL_PRESSED);
                break;
    
            case SDL_MOUSEWHEEL:
                if(event.wheel.y > 0) { processkey(-4, true); processkey(-4, false); }
                else if(event.wheel.y < 0) { processkey(-5, true); processkey(-5, false); }
                if(event.wheel.x > 0) { processkey(-35, true); processkey(-35, false); }
                else if(event.wheel.x < 0) { processkey(-36, true); processkey(-36, false); }
                break;
            case SDL_JOYDEVICEADDED:
            case SDL_JOYDEVICEREMOVED:
                reconfigrecontrollers();
                break;
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP:
                cbuttonevent(event.cbutton);
                break;
            case SDL_CONTROLLERAXISMOTION:
                caxismove(event.caxis);
                break;
        }
    }
    if(mousemoved) resetmousemotion();
}

void swapbuffers(bool overlay)
{
    recorder::capture(overlay);
    SDL_GL_SwapWindow(screen);
}
 
VAR(menufps, 0, 60, 1000);
VARP(maxfps, 0, 200, 1000);


VARFP(tearfree, 0, 0, 1, {
        if(tearfree) conoutf(CON_WARN, "tearfree is an experimental feature.");
        if(vsync) conoutf(CON_WARN, "tearfree not working with vsync on.");
    });
XIDENTHOOK(tearfree, IDF_EXTENDED);
VARP(tearfree_mincycletime, 0, 2000, 6000);
XIDENTHOOK(tearfree_mincycletime, IDF_EXTENDED);
VARP(tearfree_maxcompensatedelta, 10, 25, 100);
XIDENTHOOK(tearfree_maxcompensatedelta, IDF_EXTENDED);
VARP(tearfree_adjustcompensate, -33000, 0, 33000);
XIDENTHOOK(tearfree_adjustcompensate, IDF_EXTENDED);


#ifdef __APPLE__

#include <mach/mach_time.h>
static inline ullong tick_nsec(){
        static mach_timebase_info_data_t tb;
        if(!tb.denom) mach_timebase_info(&tb);
        return (mach_absolute_time()*ullong(tb.numer))/tb.denom;
}

static inline void sleepwrapper(llong sec, llong nsec) {
    timespec t, _;
    t.tv_sec = (time_t)sec;
    t.tv_nsec = (long)nsec;
    nanosleep(&t, &_);
}

#define main SDL_main

#elif WIN32

typedef long (__stdcall *FPNtDelayExecution)(BOOLEAN arg1, PLARGE_INTEGER arg2);

FPNtDelayExecution NtDelayExecution;

static inline ullong tick_nsec() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ullong currenttick = (ullong)ft.dwLowDateTime + ((ullong)(ft.dwHighDateTime) << 32);
    currenttick *= 100ULL;
    return currenttick;
}

static inline void sleepwrapper(llong sec, llong nsec) {
    LARGE_INTEGER time;
    time.QuadPart = (LONGLONG)(sec * 10000000LL + nsec/100LL);
    NtDelayExecution(false, &time);
}

static void initntdllprocs() {
    HMODULE hModule=GetModuleHandle(TEXT("ntdll.dll"));
    if(!hModule) fatal("Can't open ntdll.dll");
    NtDelayExecution = (FPNtDelayExecution)GetProcAddress(hModule, "NtDelayExecution");
    if(!NtDelayExecution) fatal("Can't load function NtDelayExecution");
}

#else

static inline ullong tick_nsec(){
    timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000000000ULL + t.tv_nsec;
}

static inline void sleepwrapper(llong sec, llong nsec) {
    timespec t, _;
    t.tv_sec = (time_t)sec;
    t.tv_nsec = (long)nsec;
    nanosleep(&t, &_);
}

#endif

#define NFPS 9
llong currentfps[9] = {0, // 0. fps
                       0, // 1. ifps (not used)
                       0, // 2. average draw time
                       0, // 3. OS scheduler error
                       0, // 4. average draw time jump (not showed)
                       0, // 5. tearfree sync fps
                       0, // 6. tearfree absolute error sum
                       0, // 7. tearfree draw time wait
                       0, // 8. tearfree draw error
};

void updatefpsalt(int which, int value = 1) {
        static llong fpsaccumulator[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
        static int fpsbasemillis = 0, drawmillistot = 0, syncupdates = 0;
	if(totalmillis - fpsbasemillis >= 1000){
		loopi(9){
			currentfps[i] = fpsaccumulator[i];
			fpsaccumulator[i] = 0;
		}
		if(drawmillistot){
                        currentfps[2]/=drawmillistot;
                        currentfps[4]/=drawmillistot;
                        currentfps[7]/=drawmillistot;
                }
                if(syncupdates) {
                    currentfps[5]/=syncupdates;
                    currentfps[6]/=syncupdates;
                }
		drawmillistot = 0;
                syncupdates = 0;
		fpsbasemillis = totalmillis;
	}
	fpsaccumulator[which]+=value;
	if(which==2) drawmillistot++;
	if(which==5) syncupdates++;
}

int getfpsalt(int id)
{
    int n = clamp(id, 0, NFPS-1);
    return currentfps[n];
}

VARP(tearfree_expectedtimererror, 0, 200, 1000);
XIDENTHOOK(tearfree_expectedtimererror, IDF_EXTENDED);

VARP(tearfree_allowbusywait, 0, 1, 1);
XIDENTHOOK(tearfree_allowbusywait, IDF_EXTENDED);

void precisenanosleep(ullong nsec) {
    if(!nsec) return;
    ullong start = tick_nsec();
    if(nsec > tearfree_expectedtimererror * 1000ULL || !tearfree_allowbusywait) {
        ullong delta = tearfree_allowbusywait ? tearfree_expectedtimererror * 1000 : 0;
        ullong t = nsec - delta;
        sleepwrapper(0, t);
    }
    while(tick_nsec() < nsec + start){};
}

ullong calcnextdraw( ullong lastdraw, ullong &tick_now) {
        ullong timestamp = syncwin->getlastsyncstamp();
        uint curentsyncinterval = syncwin->getsyncinterval();
        tick_now = tick_nsec();

        if(tearfree_adjustcompensate) {
            int adjustns = tearfree_adjustcompensate*1000;
            adjustns = adjustns < 0 ? max(adjustns, - static_cast<int>(curentsyncinterval)) : min(adjustns, static_cast<int>(curentsyncinterval));
            ullong adjustedtimestamp = timestamp > abs(adjustns) ? timestamp + adjustns : timestamp;
            ullong prevadjustedtimestamp = adjustedtimestamp > curentsyncinterval ? adjustedtimestamp - curentsyncinterval : 0;
            if(adjustns >= 0) {
                timestamp = adjustedtimestamp <= tick_now ? adjustedtimestamp : prevadjustedtimestamp;
            } else {
                timestamp = adjustedtimestamp + curentsyncinterval <= tick_now ? adjustedtimestamp + curentsyncinterval : adjustedtimestamp;
            }
        }

        ullong shifttime = tearfree_mincycletime*1000LL;
        timestamp = timestamp >  shifttime? timestamp - shifttime : timestamp;

        uint syncfps = static_cast<uint>(floor(1000000000000L/curentsyncinterval + 0.5));
        ullong nextdrawtime = lastdraw + curentsyncinterval;
        llong maxdelta = tearfree_maxcompensatedelta*1000;
        llong error = 0;
        if( timestamp > lastdraw && timestamp - lastdraw > curentsyncinterval/2) {
            ullong prevtimestamp = timestamp > curentsyncinterval ? timestamp - curentsyncinterval : timestamp;
            error = static_cast<llong>(lastdraw) - static_cast<llong>(prevtimestamp);
        } else {
            error =  static_cast<llong>(lastdraw) - static_cast<llong>(timestamp);
        }
        llong delta = error < 0 ? max(error, -maxdelta) : min(error, maxdelta);
        ullong nextdraw = nextdrawtime - delta;

        updatefpsalt(6, abs(error/1000));
        updatefpsalt(5, syncfps);
        return nextdraw;
}

void limitfpsalt(ullong &tick_now)
{
    static ullong lastdraw = 0;
    if(!lastdraw) lastdraw = tick_now;
    int fpslimit = 0;
    ullong nextdraw = tick_now;
    if(!vsync && tearfree) {
        if(!syncwin) syncwin = new SyncWindow;
        if(!checksyncwin()) {
            delete syncwin;
            syncwin = NULL;
            tearfree = 0;
            return limitfpsalt(tick_now);
        }
        if(tearfree_method && (syncwin->synctype != tearfree_method)) {
            delete syncwin;
            syncwin = new SyncWindow;
            return limitfpsalt(tick_now);
        }
        fpslimit = 1;
        nextdraw = calcnextdraw(lastdraw, tick_now);
    } else {
        if(syncwin) {
            delete syncwin;
            syncwin = NULL;
        }
        fpslimit = (mainmenu || minimized) && menufps ? (maxfps ? min(maxfps, menufps) : menufps) : maxfps;
        nextdraw = (fpslimit ? 1000000000ULL / fpslimit : 0) + lastdraw;
        updatefpsalt(5, 0);
        updatefpsalt(6, 0);
    }
    if(!vsync && tearfree) {
        if( !(fpslimit && nextdraw < tick_now) ) {
            ullong t = max(0ULL, nextdraw - tick_now);
            precisenanosleep(t);
        }
    } else {
        if(! (nextdraw < tick_now)) {
            ullong t = max(0ULL, nextdraw - tick_now);
            precisenanosleep(t);
        }
    }
    tick_now = tick_nsec();
    if(vsync || !maxfps) updatefpsalt(3,0);
    else updatefpsalt(3, (tick_now - nextdraw)/1000);
    lastdraw = tick_now;
    return;
}

void limitfps(int &millis, int curmillis)
{
    int limit = (mainmenu || minimized) && menufps ? (maxfps ? min(maxfps, menufps) : menufps) : maxfps;
    if(!limit) return;
    static int fpserror = 0;
    int delay = 1000/limit - (millis-curmillis);
    if(delay < 0) fpserror = 0;
    else
    {
        fpserror += 1000%limit;
        if(fpserror >= limit)
        {
            ++delay;
            fpserror -= limit;
        }
        if(delay > 0)
        {
            SDL_Delay(delay);
            millis += delay;
        }
    }
}


#if defined(WIN32) && !defined(_DEBUG) && !defined(__GNUC__)
void stackdumper(unsigned int type, EXCEPTION_POINTERS *ep)
{
    if(!ep) fatal("unknown type");
    EXCEPTION_RECORD *er = ep->ExceptionRecord;
    CONTEXT *context = ep->ContextRecord;
    string out, t;
    formatstring(out)("Cube 2: Sauerbraten Win32 Exception: 0x%x [0x%x]\n\n", er->ExceptionCode, er->ExceptionCode==EXCEPTION_ACCESS_VIOLATION ? er->ExceptionInformation[1] : -1);
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
#ifdef _AMD64_
	STACKFRAME64 sf = {{context->Rip, 0, AddrModeFlat}, {}, {context->Rbp, 0, AddrModeFlat}, {context->Rsp, 0, AddrModeFlat}, 0};
    while(::StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), GetCurrentThread(), &sf, context, NULL, ::SymFunctionTableAccess, ::SymGetModuleBase, NULL))
	{
		union { IMAGEHLP_SYMBOL64 sym; char symext[sizeof(IMAGEHLP_SYMBOL64) + sizeof(string)]; };
		sym.SizeOfStruct = sizeof(sym);
		sym.MaxNameLength = sizeof(symext) - sizeof(sym);
		IMAGEHLP_LINE64 line;
		line.SizeOfStruct = sizeof(line);
        DWORD64 symoff;
		DWORD lineoff;
        if(SymGetSymFromAddr64(GetCurrentProcess(), sf.AddrPC.Offset, &symoff, &sym) && SymGetLineFromAddr64(GetCurrentProcess(), sf.AddrPC.Offset, &lineoff, &line))
#else
    STACKFRAME sf = {{context->Eip, 0, AddrModeFlat}, {}, {context->Ebp, 0, AddrModeFlat}, {context->Esp, 0, AddrModeFlat}, 0};
    while(::StackWalk(IMAGE_FILE_MACHINE_I386, GetCurrentProcess(), GetCurrentThread(), &sf, context, NULL, ::SymFunctionTableAccess, ::SymGetModuleBase, NULL))
	{
		union { IMAGEHLP_SYMBOL sym; char symext[sizeof(IMAGEHLP_SYMBOL) + sizeof(string)]; };
		sym.SizeOfStruct = sizeof(sym);
		sym.MaxNameLength = sizeof(symext) - sizeof(sym);
		IMAGEHLP_LINE line;
		line.SizeOfStruct = sizeof(line);
        DWORD symoff, lineoff;
        if(SymGetSymFromAddr(GetCurrentProcess(), sf.AddrPC.Offset, &symoff, &sym) && SymGetLineFromAddr(GetCurrentProcess(), sf.AddrPC.Offset, &lineoff, &line))
#endif
        {
            char *del = strrchr(line.FileName, '\\');
            formatstring(t)("%s - %s [%d]\n", sym.Name, del ? del + 1 : line.FileName, line.LineNumber);
            concatstring(out, t);
        }
    }
    fatal(out);
}
#endif

#define MAXFPSHISTORY 60

int fpspos = 0, fpshistory[MAXFPSHISTORY];

void resetfpshistory()
{
    loopi(MAXFPSHISTORY) fpshistory[i] = 1;
    fpspos = 0;
}

void updatefpshistory(int millis)
{
    fpshistory[fpspos++] = max(1, min(1000, millis));
    if(fpspos>=MAXFPSHISTORY) fpspos = 0;
}

void getfps(int &fps, int &bestdiff, int &worstdiff)
{
    int total = fpshistory[MAXFPSHISTORY-1], best = total, worst = total;
    loopi(MAXFPSHISTORY-1)
    {
        int millis = fpshistory[i];
        total += millis;
        if(millis < best) best = millis;
        if(millis > worst) worst = millis;
    }
    fps = (1000*MAXFPSHISTORY)/max(1, total);
    bestdiff = 1000/max(1, best) - fps;
    worstdiff = fps - 1000/max(1, worst);
}

void getfps_(int *raw)
{
    int fps, bestdiff, worstdiff;
    if(*raw) fps = 1000/max(1, fpshistory[(fpspos+MAXFPSHISTORY-1)%MAXFPSHISTORY]);
    else getfps(fps, bestdiff, worstdiff);
    intret(fps);
}

COMMANDN(getfps, getfps_, "i");


bool inbetweenframes = false, renderedframe = true;

static bool findarg(int argc, char **argv, const char *str)
{
    for(int i = 1; i<argc; i++) if(strstr(argv[i], str)==argv[i]) return true;
    return false;
}

static int clockrealbase = 0, clockvirtbase = 0;
static void clockreset() { clockrealbase = SDL_GetTicks(); clockvirtbase = totalmillis; }
VARFP(clockerror, 990000, 1000000, 1010000, clockreset());
VARFP(clockfix, 0, 0, 1, clockreset());

int getclockmillis()
{
    int millis = SDL_GetTicks() - clockrealbase;
    if(clockfix) millis = int(millis*(double(clockerror)/1000000));
    millis += clockvirtbase;
    return max(millis, totalmillis);
}

VAR(numcpus, 1, 1, 16);

struct args {
    int argc;
    char** argv;
};

int gameloop (void* p)
{
    struct args* a = (struct args*)p;
    int argc = a->argc;
    char** argv = a->argv;

    setlogfile(NULL);

    int dedicated = 0;
    char *load = NULL, *initscript = NULL;

    initing = INIT_RESET;
    for(int i = 1; i<argc; i++)
    {
        if(argv[i][0]=='-') switch(argv[i][1])
        {
            case 'q': 
			{
				const char *dir = sethomedir(&argv[i][2]);
				if(dir) logoutf("Using home directory: %s", dir);
				break;
			}
        }
    }
    execfile("init.cfg", false);
    for(int i = 1; i<argc; i++)
    {
        if(argv[i][0]=='-') switch(argv[i][1])
        {
            case 'q': /* parsed first */ break;
            case 'r': /* compat, ignore */ break;
            case 'k':
            {
                const char *dir = addpackagedir(&argv[i][2]);
                if(dir) logoutf("Adding package directory: %s", dir);
                break;
            }
            case 'g': logoutf("Setting log file: %s", &argv[i][2]); setlogfile(&argv[i][2]); break;
            case 'd': dedicated = atoi(&argv[i][2]); if(dedicated<=0) dedicated = 2; break;
            case 'w': scr_w = clamp(atoi(&argv[i][2]), SCR_MINW, SCR_MAXW); if(!findarg(argc, argv, "-h")) scr_h = -1; break;
            case 'h': scr_h = clamp(atoi(&argv[i][2]), SCR_MINH, SCR_MAXH); if(!findarg(argc, argv, "-w")) scr_w = -1; break;
            case 'z': depthbits = atoi(&argv[i][2]); break;
            case 'b': /* compat, ignore */ break;
            case 'a': fsaa = atoi(&argv[i][2]); break;
            case 'v': vsync = atoi(&argv[i][2]); if(vsync < 0) { vsynctear = 1; vsync = 1; } else vsynctear = 0; break;
            case 't': fullscreen = atoi(&argv[i][2]); break;
            case 's': stencilbits = atoi(&argv[i][2]); break;
            case 'f': 
            {
                extern int useshaders, shaderprecision, forceglsl;
                int sh = -1, prec = shaderprecision;
                for(int j = 2; argv[i][j]; j++) switch(argv[i][j])
                {
                    case 'a': case 'A': forceglsl = 0; sh = 1; break;
                    case 'g': case 'G': forceglsl = 1; sh = 1; break;
                    case 'f': case 'F': case '0': sh = 0; break;
                    case '1': case '2': case '3': if(sh < 0) sh = 1; prec = argv[i][j] - '1'; break;
                    default: break;
                }
                useshaders = sh > 0 ? 1 : 0;
                shaderprecision = prec;
                break;
            }
            case 'l': 
            {
                char pkgdir[] = "packages/"; 
                load = strstr(path(&argv[i][2]), path(pkgdir)); 
                if(load) load += sizeof(pkgdir)-1; 
                else load = &argv[i][2]; 
                break;
            }
            case 'x': initscript = &argv[i][2]; break;
            default: if(!serveroption(argv[i])) gameargs.add(argv[i]); break;
        }
        else gameargs.add(argv[i]);
    }

    initing = NOT_INITING;

    numcpus = clamp(SDL_GetCPUCount(), 1, 16);

    if(dedicated <= 1)
    {
        logoutf("init: sdl");

        if(SDL_Init(SDL_INIT_TIMER|SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_JOYSTICK|SDL_INIT_GAMECONTROLLER|SDL_INIT_HAPTIC)<0)
            fatal("Unable to initialize SDL: %s", SDL_GetError());
        SDL_version compiled;
        SDL_version linked;
        SDL_VERSION(&compiled);
        SDL_GetVersion(&linked);
        logoutf("Compiled against SDL version %d.%d.%d",
                compiled.major, compiled.minor, compiled.patch);
        logoutf("Linking against SDL version %d.%d.%d",
                linked.major, linked.minor, linked.patch);
    }
    
    logoutf("init: net");
    if(enet_initialize()<0) fatal("Unable to initialise network module");
    atexit(enet_deinitialize);
    enet_time_set(0);

    logoutf("init: game");
    game::parseoptions(gameargs);
    initserver(dedicated>0, dedicated>1);  // never returns if dedicated
    ASSERT(dedicated <= 1);
    game::initclient();

    logoutf("init: video");
    SDL_SetHint(SDL_HINT_GRAB_KEYBOARD, "0");
    int useddepthbits = 0, usedfsaa = 0;
    setupscreen(useddepthbits, usedfsaa);
    SDL_ShowCursor(SDL_FALSE);
    SDL_StopTextInput(); // workaround for spurious text-input events getting sent on first text input toggle?

    logoutf("init: gl");
    gl_checkextensions();
    gl_init(useddepthbits, usedfsaa);
    notexture = textureload("packages/textures/notexture.png");
    if(!notexture) fatal("could not find core textures");

    logoutf("init: console");
    if(!execfile("data/stdlib.cfg", false)) fatal("cannot find data files (you are running from the wrong folder, try .bat file in the main folder)");   // this is the first file we load.
    if(!execfile("data/font.cfg", false)) fatal("cannot find font definitions");
    if(!setfont("default")) fatal("no default font specified");

    inbetweenframes = true;
    renderbackground("initializing...");

    logoutf("init: effects");
    loadshaders();
    particleinit();
    initdecals();

    logoutf("init: world");
    camera1 = player = game::iterdynents(0);
    emptymap(0, true, NULL, false);

    logoutf("init: sound");
    initsound();

    logoutf("init: cfg");
    execfile("data/keymap.cfg");
    execfile("data/stdedit.cfg");
    execfile("data/menus.cfg");
    execfile("data/sounds.cfg");
    execfile("data/brush.cfg");
    execfile("mybrushes.cfg", false);
    if(game::savedservers()) execfile(game::savedservers(), false);
    if(game::ignoredservers()) execfile(game::ignoredservers(), false);
    
    identflags |= IDF_PERSIST;
    
    initing = INIT_LOAD;
    if(!execfile(game::savedconfig(), false)) 
    {
        execfile(game::defaultconfig());
        writecfg(game::restoreconfig());
    }
    execfile("extendedconfig.cfg", false);
    execfile(game::autoexec(), false);

    if(game::getgamescripts()) {
        const char **gamescripts = game::getgamescripts();
        if(gamescripts) {
            logoutf("init: extended game scripts");
            identflags &= ~IDF_PERSIST;
            for (int i = 0; gamescripts[i] != 0; i++)  {
                executestr(gamescripts[i]);
            }
            identflags |= IDF_PERSIST;
        }
    }

    initing = NOT_INITING;

    identflags &= ~IDF_PERSIST;

    string gamecfgname;
    copystring(gamecfgname, "data/game_");
    concatstring(gamecfgname, game::gameident());
    concatstring(gamecfgname, ".cfg");
    execfile(gamecfgname);
    
    game::loadconfigs();

    identflags |= IDF_PERSIST;

    if(execfile("once.cfg", false)) remove(findfile("once.cfg", "rb"));

    if(load)
    {
        logoutf("init: localconnect");
        //localconnect();
        game::changemap(load);
    }

    if(initscript) execute(initscript);

    logoutf("init: mainloop");

    initmumble();
    resetfpshistory();

    inputgrab(grabinput = true);
    ignoremousemotion();

    conoutf("\f0Sauerbraten SDL2 Client\f1 Version 2.2.2");

    ullong prevcycletime = 0;
    for(;;)
    {
        static int frames = 0;
        int millis = getclockmillis();
        ullong tick = tick_nsec();
        if(!vsync && tearfree) {
            limitfpsalt(tick);
            millis = getclockmillis();
        } else {
            limitfps(millis, totalmillis);
        }
        elapsedtime = millis - totalmillis;
        static int timeerr = 0;
        int scaledtime = game::scaletime(elapsedtime) + timeerr;
        curtime = scaledtime/100;
        timeerr = scaledtime%100;
        if(!multiplayer(false) && curtime>200) curtime = 200;
        if(game::ispaused()) curtime = 0;
        lastmillis += curtime;
        totalmillis = millis;
        updatetime();
 
        checkinput();
        menuprocess();
        tryedit();

        if(lastmillis) game::updateworld();
        game::checkgameinfo();

        checksleep(lastmillis);

        serverslice(false, 0);

        updatefpsalt(1);
        updatefpsalt(0);

        ullong drawstart = tick_nsec();

        if(frames) updatefpshistory(elapsedtime);
        frames++;

        // miscellaneous general game effects
        recomputecamera();
        updateparticles();
        updatesounds();

        if(minimized) continue;
        if(shouldminimize) {
            shouldminimize = false;
            SDL_MinimizeWindow(screen);
            continue;
        }

        ullong cyclestart = tick_nsec();
        inbetweenframes = false;
        if(mainmenu) gl_drawmainmenu();
        else gl_drawframe();
        framehasgui = false;
        glFinish();

        ullong endtick = tick_nsec();

        // additional time before swapping buffers
        if(!vsync && tearfree) {
            if(cyclestart+(1000ULL*tearfree_mincycletime) > endtick) {
                ullong t = (cyclestart+(1000ULL*tearfree_mincycletime))-endtick;
                precisenanosleep(t);
          }
        }

        renderedframe = inbetweenframes = true;
        ullong drawend = tick_nsec();
        swapbuffers();

        ullong lastwaittime = drawend - endtick;
        ullong lastdrawtime = drawend - drawstart;
        ullong lastcycletime = drawend - cyclestart;

        if(lastdrawtime) {
            updatefpsalt(2, lastdrawtime/1000);
            updatefpsalt(7, lastwaittime/1000);
            if(lastcycletime && prevcycletime) {
                if(lastcycletime > tearfree_mincycletime*1000U) updatefpsalt(8, (lastcycletime - tearfree_mincycletime*1000U)/1000);
                else updatefpsalt(8, 0);
                if(vsync) updatefpsalt(4,0);
                else lastcycletime > prevcycletime ? updatefpsalt(4, (lastcycletime - prevcycletime)/1000) : updatefpsalt(4, (prevcycletime - lastcycletime)/1000);
            }
            prevcycletime = lastcycletime;
        }
    }
    return 0;
}

#ifdef __APPLE__
extern "C" {
int mymain(int argc, char **argv)
#else
int main(int argc, char **argv)
#endif
{
    #if defined(WIN32) && !defined(_DEBUG) && !defined(__GNUC__)
    // atexit((void (__cdecl *)(void))_CrtDumpMemoryLeaks);
    __try {
    #endif

    #if defined(WIN32)
    initntdllprocs();
    #endif

    #if !defined(WIN32) && !defined(_DEBUG) && defined(__GNUC__)
    signal(SIGFPE, handler);
    signal(SIGSEGV, handler);
    signal(SIGBUS, handler);
    signal(SIGABRT, handler);
    #endif

    struct args a;
    a.argc = argc;
    a.argv = argv;
    SDL_Thread* main_thread = SDL_CreateThread(gameloop, "mainloop", (void *)(&a));
    SDL_WaitThread(main_thread, NULL);

    ASSERT(0);   
    return EXIT_FAILURE;

    #if defined(WIN32) && !defined(_DEBUG) && !defined(__GNUC__)
    } __except(stackdumper(0, GetExceptionInformation()), EXCEPTION_CONTINUE_SEARCH) { return 0; }
    #endif
}

#ifdef __APPLE__
}
#endif

