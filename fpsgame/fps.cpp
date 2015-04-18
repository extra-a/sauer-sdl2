#include "game.h"
#include "extendedscripts.h"

extern int getpacketloss();
extern int guiautotab;
extern string homedir;


float staticscale = 0.33;
VARFP(hudscale, 50, 100, 300, { staticscale = 0.33*(hudscale/100.0f); })
XIDENTHOOK(hudscale, IDF_EXTENDED);


#include <sys/stat.h>

namespace game
{
    bool intermission = false;
    int maptime = 0, maprealtime = 0, maplimit = -1;
    int respawnent = -1;
    int lasthit = 0, lastspawnattempt = 0;

    int following = -1, followdir = 0;

    fpsent *player1 = NULL;         // our client
    vector<fpsent *> players;       // other clients
    int savedammo[NUMGUNS];

    bool clientoption(const char *arg) { return false; }

    static inline int limitscore(int s) {
        return clamp(s, -999, 9999);
    }

    static inline int limitammo(int s) {
        return clamp(s, 0, 999);
    }

    void taunt()
    {
        if(player1->state!=CS_ALIVE || player1->physstate<PHYS_SLOPE) return;
        if(lastmillis-player1->lasttaunt<1000) return;
        player1->lasttaunt = lastmillis;
        addmsg(N_TAUNT, "rc", player1);
    }
    COMMAND(taunt, "");

    ICOMMAND(getfollow, "", (),
    {
        fpsent *f = followingplayer();
        intret(f ? f->clientnum : -1);
    });

    void follow(char *arg)
    {
        if(arg[0] ? player1->state==CS_SPECTATOR : following>=0)
        {
            following = arg[0] ? parseplayer(arg) : -1;
            if(following==player1->clientnum) following = -1;
            followdir = 0;
            conoutf("follow %s", following>=0 ? "on" : "off");
        }
    }
    COMMAND(follow, "s");

    void nextfollow(int dir)
    {
        if(player1->state!=CS_SPECTATOR || clients.empty())
        {
            stopfollowing();
            return;
        }
        int cur = following >= 0 ? following : (dir < 0 ? clients.length() - 1 : 0);
        loopv(clients)
        {
            cur = (cur + dir + clients.length()) % clients.length();
            if(clients[cur] && clients[cur]->state!=CS_SPECTATOR)
            {
                if(following<0) conoutf("follow on");
                following = cur;
                followdir = dir;
                return;
            }
        }
        stopfollowing();
    }
    ICOMMAND(nextfollow, "i", (int *dir), nextfollow(*dir < 0 ? -1 : 1));


    const char *getclientmap() { return clientmap; }

    void resetgamestate()
    {
        if(m_classicsp)
        {
            clearmovables();
            clearmonsters();                 // all monsters back at their spawns for editing
            entities::resettriggers();
        }
        clearprojectiles();
        clearbouncers();
    }

    fpsent *spawnstate(fpsent *d)              // reset player state not persistent accross spawns
    {
        d->respawn();
        d->spawnstate(gamemode);
        return d;
    }

    void respawnself()
    {
        if(ispaused()) return;
        if(m_mp(gamemode))
        {
            int seq = (player1->lifesequence<<16)|((lastmillis/1000)&0xFFFF);
            if(player1->respawned!=seq) { addmsg(N_TRYSPAWN, "rc", player1); player1->respawned = seq; }
        }
        else
        {
            spawnplayer(player1);
            showscores(false);
            lasthit = 0;
            if(cmode) cmode->respawned(player1);
        }
    }

    fpsent *pointatplayer()
    {
        loopv(players) if(players[i] != player1 && intersect(players[i], player1->o, worldpos)) return players[i];
        return NULL;
    }

    void stopfollowing()
    {
        if(following<0) return;
        following = -1;
        followdir = 0;
        conoutf("follow off");
    }

    fpsent *followingplayer()
    {
        if(player1->state!=CS_SPECTATOR || following<0) return NULL;
        fpsent *target = getclient(following);
        if(target && target->state!=CS_SPECTATOR) return target;
        return NULL;
    }

    VARP(usefollowingplayerteam, 0, 0, 1);
    XIDENTHOOK(usefollowingplayerteam, IDF_EXTENDED);
    
    const char* getcurrentteam() {
        if(!usefollowingplayerteam) return player1->team;
        return (player1->state==CS_SPECTATOR && followingplayer()) ? followingplayer()->team : player1->team;
    }

    fpsent *hudplayer()
    {
        if(thirdperson) return player1;
        fpsent *target = followingplayer();
        return target ? target : player1;
    }

    void setupcamera()
    {
        fpsent *target = followingplayer();
        if(target)
        {
            player1->yaw = target->yaw;
            player1->pitch = target->state==CS_DEAD ? 0 : target->pitch;
            player1->o = target->o;
            player1->resetinterp();
        }
    }

    bool detachcamera()
    {
        fpsent *d = hudplayer();
        return d->state==CS_DEAD;
    }

    bool collidecamera()
    {
        switch(player1->state)
        {
            case CS_EDITING: return false;
            case CS_SPECTATOR: return followingplayer()!=NULL;
        }
        return true;
    }

    VARP(smoothmove, 0, 75, 100);
    VARP(smoothdist, 0, 32, 64);

    void predictplayer(fpsent *d, bool move)
    {
        d->o = d->newpos;
        d->yaw = d->newyaw;
        d->pitch = d->newpitch;
        d->roll = d->newroll;
        if(move)
        {
            moveplayer(d, 1, false);
            d->newpos = d->o;
        }
        float k = 1.0f - float(lastmillis - d->smoothmillis)/smoothmove;
        if(k>0)
        {
            d->o.add(vec(d->deltapos).mul(k));
            d->yaw += d->deltayaw*k;
            if(d->yaw<0) d->yaw += 360;
            else if(d->yaw>=360) d->yaw -= 360;
            d->pitch += d->deltapitch*k;
            d->roll += d->deltaroll*k;
        }
    }

    void otherplayers(int curtime)
    {
        loopv(players)
        {
            fpsent *d = players[i];
            if(d == player1 || d->ai) continue;

            if(d->state==CS_DEAD && d->ragdoll) moveragdoll(d);
            else if(!intermission)
            {
                if(lastmillis - d->lastaction >= d->gunwait) d->gunwait = 0;
                if(d->quadmillis) entities::checkquad(curtime, d);
            }

            const int lagtime = totalmillis-d->lastupdate;
            if(!lagtime || intermission) continue;
            else if(lagtime>1000 && d->state==CS_ALIVE)
            {
                d->state = CS_LAGGED;
                continue;
            }
            if(d->state==CS_ALIVE || d->state==CS_EDITING)
            {
                if(smoothmove && d->smoothmillis>0) predictplayer(d, true);
                else moveplayer(d, 1, false);
            }
            else if(d->state==CS_DEAD && !d->ragdoll && lastmillis-d->lastpain<2000) moveplayer(d, 1, true);
        }
    }

    VARFP(slowmosp, 0, 0, 1, { if(m_sp && !slowmosp) server::forcegamespeed(100); }); 

    void checkslowmo()
    {
        static int lastslowmohealth = 0;
        server::forcegamespeed(intermission ? 100 : clamp(player1->health, 25, 200));
        if(player1->health<player1->maxhealth && lastmillis-max(maptime, lastslowmohealth)>player1->health*player1->health/2)
        {
            lastslowmohealth = lastmillis;
            player1->health++;
        }
    }

    extern int getgundamagetotal(int gun, fpsent* f);
    extern double getweaponaccuracy(int gun, fpsent* f);
    extern int getgundamagedealt(int gun, fpsent* f);
    extern int getgundamagereceived(int gun, fpsent* f);
    extern int getgunnetdamage(int gun, fpsent* f);

    #define BLEN 200
    static char buff[BLEN];
    void printplayerstats(fpsent *d) {
        if(!d) return;

        #define ROWS 7
        const char* weapname[ROWS] = {"SAW", "SG", "CG", "RL", "RF", "GL", "PI"};
        int len = 0;
        if(getgundamagetotal(-1, d) == 0) return;
        printf("---------------------\n");
        if(d->team && strlen(d->team) && m_teammode) {
            logoutf("%s(%d): scored %d, frags %d, net %d (team: %s)\n", d->name, d->clientnum, d->flags, d->frags, d->frags - d->deaths, d->team);
        } else {
            logoutf("%s(%d): scored %d, frags %d, net %d\n", d->name, d->clientnum, d->flags, d->frags, d->frags - d->deaths);
        }

        len = 0;
        len += snprintf(buff+len, max(0, BLEN-len), "%-10s","weapon");
        loopi(ROWS) {
            len += snprintf(buff+len, max(0, BLEN-len), "%-10s", weapname[i]);
        }
        snprintf(buff+len, max(0, BLEN-len), "%-10s","total");
        logoutf("%s", buff);

        len = 0;
        len += snprintf(buff+len, max(0, BLEN-len), "%-10s", "accuracy");
        loopi(ROWS) {
            len += snprintf(buff+len, max(0, BLEN-len),"%-10.2lf", getweaponaccuracy(i, d));
        }
        snprintf(buff+len, max(0, BLEN-len), "%-10.2lf", getweaponaccuracy(-1, d));
        logoutf("%s", buff);

        len = 0;
        len += snprintf(buff+len, max(0, BLEN-len), "%-10s", "damage");
        loopi(ROWS) {
            len += snprintf(buff+len, max(0, BLEN-len), "%-10d", getgundamagedealt(i, d));
        }
        snprintf(buff+len, max(0, BLEN-len), "%-10d", getgundamagedealt(-1, d));
        logoutf("%s", buff);

        len = 0;
        len += snprintf(buff+len, max(0, BLEN-len), "%-10s", "taken");
        loopi(ROWS) {
            len += snprintf(buff+len, max(0, BLEN-len), "%-10d", getgundamagereceived(i, d));
        }
        snprintf(buff+len, max(0, BLEN-len), "%-10d", getgundamagereceived(-1, d));
        logoutf("%s", buff);

        len = 0;
        len += snprintf(buff+len, max(0, BLEN-len), "%-10s", "net");
        loopi(ROWS) {
            len += snprintf(buff+len, max(0, BLEN-len), "%-10d", getgunnetdamage(i, d));
        }
        snprintf(buff+len, max(0, BLEN-len), "%-10d", getgunnetdamage(-1, d));
        logoutf("%s", buff);

        #undef ROWS
    }
    #undef BLEN

    extern void getteamscores(vector<teamscore> &ts);

    void dumpstats() {
        if(m_teammode) {
            vector<teamscore> teamscores;
            getteamscores(teamscores);
            printf("---------------------\n");
            loopv(teamscores) {
                logoutf("%s: %d", teamscores[i].team, teamscores[i].score);
            }
        }
        loopv(clients) {
            fpsent *d = clients[i];
            printplayerstats(d);
        }
        printplayerstats(player1);
        printf("---------------------\n");
    }

    VARP(dumpstatsongameend, 0, 0, 1);
    XIDENTHOOK(dumpstatsongameend, IDF_EXTENDED);
    ICOMMAND(dumpstats, "", (), dumpstats());

    extern void checkseserverinfo();
    extern void checkextinfos();
    extern void checkservergameinfo();
    extern void autodemocheck();
    extern int needplayersearch;
    extern int needpreviewupdate;
    void checkgameinfo() {
        checkseserverinfo();
        checkextinfos();
        checkservergameinfo();
        autodemocheck();
        if(needplayersearch) {
            forceinitservers();
            refreshservers();
        }
        needpreviewupdate = 0;
        needplayersearch = 0;
    }

    extern void checkseek();
    void updateworld()        // main game update loop
    {
        if(!maptime) { maptime = lastmillis; maprealtime = totalmillis; return; }
        if(!curtime) { gets2c(); if(player1->clientnum>=0) c2sinfo(); return; }

        physicsframe();
        ai::navigate();
        if(player1->state != CS_DEAD && !intermission)
        {
            if(player1->quadmillis) entities::checkquad(curtime, player1);
        }
        updateweapons(curtime);
        otherplayers(curtime);
        ai::update();
        moveragdolls();
        gets2c();
        updatemovables(curtime);
        updatemonsters(curtime);
        if(connected)
        {
            if(player1->state == CS_DEAD)
            {
                if(player1->ragdoll) moveragdoll(player1);
                else if(lastmillis-player1->lastpain<2000)
                {
                    player1->move = player1->strafe = 0;
                    moveplayer(player1, 10, true);
                }
            }
            else if(!intermission)
            {
                if(player1->ragdoll) cleanragdoll(player1);
                moveplayer(player1, 10, true);
                swayhudgun(curtime);
                entities::checkitems(player1);
                if(m_sp)
                {
                    if(slowmosp) checkslowmo();
                    if(m_classicsp) entities::checktriggers();
                }
                else if(cmode) cmode->checkitems(player1);
            }
        }
        if(player1->clientnum>=0) c2sinfo();   // do this last, to reduce the effective frame lag
        checkseek();
    }

    void spawnplayer(fpsent *d)   // place at random spawn
    {
        if(cmode) cmode->pickspawn(d);
        else findplayerspawn(d, d==player1 && respawnent>=0 ? respawnent : -1);
        spawnstate(d);
        if(d==player1)
        {
            if(editmode) d->state = CS_EDITING;
            else if(d->state != CS_SPECTATOR) d->state = CS_ALIVE;
        }
        else d->state = CS_ALIVE;
    }

    VARP(spawnwait, 0, 0, 1000);

    void respawn()
    {
        if(player1->state==CS_DEAD)
        {
            player1->attacking = false;
            int wait = cmode ? cmode->respawnwait(player1) : 0;
            if(wait>0)
            {
                lastspawnattempt = lastmillis;
                //conoutf(CON_GAMEINFO, "\f2you must wait %d second%s before respawn!", wait, wait!=1 ? "s" : "");
                return;
            }
            if(lastmillis < player1->lastpain + spawnwait) return;
            if(m_dmsp) { changemap(clientmap, gamemode); return; }    // if we die in SP we try the same map again
            respawnself();
            if(m_classicsp)
            {
                conoutf(CON_GAMEINFO, "%sYou wasted another life! The monsters stole your armour and some ammo...", getmsgcolorstring());
                loopi(NUMGUNS) if(i!=GUN_PISTOL && (player1->ammo[i] = savedammo[i]) > 5) player1->ammo[i] = max(player1->ammo[i]/3, 5);
            }
        }
    }

    // inputs

    void doattack(bool on)
    {
        if(!connected || intermission) return;
        if((player1->attacking = on)) respawn();
    }

    bool canjump()
    {
        if(!connected || intermission) return false;
        respawn();
        return player1->state!=CS_DEAD;
    }

    bool allowmove(physent *d)
    {
        if(d->type!=ENT_PLAYER) return true;
        return !((fpsent *)d)->lasttaunt || lastmillis-((fpsent *)d)->lasttaunt>=1000;
    }

    VARP(hitsound, 0, 0, 1);

    void damaged(int damage, fpsent *d, fpsent *actor, bool local)
    {
        if((d->state!=CS_ALIVE && d->state != CS_LAGGED && d->state != CS_SPAWNING) || intermission) return;

        if(local) damage = d->dodamage(damage);
        else if(actor==player1) return;

        fpsent *h = hudplayer();
        if(h!=player1 && actor==h && d!=actor)
        {
            if(hitsound && lasthit != lastmillis) playsound(S_HIT);
            lasthit = lastmillis;
        }
        if(d==h)
        {
            damageblend(damage);
            damagecompass(damage, actor->o);
        }
        damageeffect(damage, d, d!=h);

		ai::damaged(d, actor);

        if(m_sp && slowmosp && d==player1 && d->health < 1) d->health = 1;

        if(d->health<=0) { if(local) killed(d, actor); }
        else if(d==h) playsound(S_PAIN6);
        else playsound(S_PAIN1+rnd(5), &d->o);
    }

    VARP(deathscore, 0, 1, 1);

    void deathstate(fpsent *d, bool restore)
    {
        d->state = CS_DEAD;
        d->lastpain = lastmillis;
        if(!restore) gibeffect(max(-d->health, 0), d->vel, d);
        if(d==player1)
        {
            if(deathscore) showscores(true);
            disablezoom();
            if(!restore) loopi(NUMGUNS) savedammo[i] = player1->ammo[i];
            d->attacking = false;
            if(!restore) d->deaths++;
            //d->pitch = 0;
            d->roll = 0;
            playsound(S_DIE1+rnd(2));
        }
        else
        {
            d->move = d->strafe = 0;
            d->resetinterp();
            d->smoothmillis = 0;
            playsound(S_DIE1+rnd(2), &d->o);
        }
    }

    VARP(teamcolorfrags, 0, 1, 1);
    VARP(fragbeep, 0, 0, 1);
    XIDENTHOOK(fragbeep, IDF_EXTENDED);

    void killed(fpsent *d, fpsent *actor)
    {
        if(d->state==CS_EDITING)
        {
            d->editstate = CS_DEAD;
            if(d==player1) d->deaths++;
            else d->resetinterp();
            return;
        }
        else if((d->state!=CS_ALIVE && d->state != CS_LAGGED && d->state != CS_SPAWNING) || intermission) return;

        fpsent *h = followingplayer();
        if(!h) h = player1;
        int contype = d==h || actor==h ? CON_FRAG_SELF : CON_FRAG_OTHER;
        const char *dname = "", *aname = "";
        if(m_teammode && teamcolorfrags)
        {
            dname = teamcolorname(d, "you");
            aname = teamcolorname(actor, "you");
        }
        else
        {
            dname = colorname(d, NULL, "", "", "you");
            aname = colorname(actor, NULL, "", "", "you");
        }
        if(actor->type==ENT_AI)
            conoutf(contype, "%s%s got killed by %s!", getmsgcolorstring(), dname, aname);
        else if(d==actor || actor->type==ENT_INANIMATE)
            conoutf(contype, "%s%s suicided%s", getmsgcolorstring(), dname, d==player1 ? "!" : "");
        else if(isteam(d->team, actor->team))
        {
            contype |= CON_TEAMKILL;
            if(actor==player1) conoutf(contype, "\f6%s fragged a teammate (%s)", aname, dname);
            else if(d==player1) conoutf(contype, "\f6%s got fragged by a teammate (%s)", dname, aname);
            else conoutf(contype, "%s%s fragged a teammate (%s)", getmsgcolorstring(), aname, dname);
        }
        else
        {
            if(d==player1) conoutf(contype, "%s%s got fragged by %s", getmsgcolorstring(), dname, aname);
            else conoutf(contype, "%s%s fragged %s", getmsgcolorstring(), aname, dname);
        }
        if(fragbeep && h==actor && !isteam(d->team, h->team) && d != h) playsound(S_FRAGBEEP);
        deathstate(d);
		ai::killed(d, actor);
    }

    extern bool lastdemoended;
    void timeupdate(int secs)
    {
        if(secs > 0)
        {
            maplimit = lastmillis + secs*1000;
        }
        else
        {
            intermission = true;
            player1->attacking = false;
            lastdemoended = true;
            if(cmode) cmode->gameover();
            conoutf(CON_GAMEINFO, "%sintermission:", getmsgcolorstring());
            if(dumpstatsongameend) dumpstats();
            conoutf(CON_GAMEINFO, "%sgame has ended!", getmsgcolorstring());
            if(m_ctf) conoutf(CON_GAMEINFO, "%splayer frags: %d, flags: %d, deaths: %d", getmsgcolorstring(), player1->frags, player1->flags, player1->deaths);
            else if(m_collect) conoutf(CON_GAMEINFO, "%splayer frags: %d, skulls: %d, deaths: %d", getmsgcolorstring(), player1->frags, player1->flags, player1->deaths);
            else conoutf(CON_GAMEINFO, "%splayer frags: %d, deaths: %d", getmsgcolorstring(), player1->frags, player1->deaths);
            int accuracy = (player1->totaldamage*100)/max(player1->totalshots, 1);
            conoutf(CON_GAMEINFO, "%splayer total damage dealt: %d, damage wasted: %d, accuracy(%%): %d", getmsgcolorstring(), player1->totaldamage, player1->totalshots-player1->totaldamage, accuracy);
            if(m_sp) spsummary(accuracy);

            showscores(true);
            disablezoom();
            
            if(identexists("intermission")) execute("intermission");
        }
    }

    ICOMMAND(getfrags, "", (), intret(player1->frags));
    ICOMMAND(getflags, "", (), intret(player1->flags));
    ICOMMAND(getdeaths, "", (), intret(player1->deaths));
    ICOMMAND(getaccuracy, "", (), intret((player1->totaldamage*100)/max(player1->totalshots, 1)));
    ICOMMAND(gettotaldamage, "", (), intret(player1->totaldamage));
    ICOMMAND(gettotalshots, "", (), intret(player1->totalshots));

    vector<fpsent *> clients;

    fpsent *newclient(int cn)   // ensure valid entity
    {
        if(cn < 0 || cn > max(0xFF, MAXCLIENTS + MAXBOTS))
        {
            neterr("clientnum", false);
            return NULL;
        }

        if(cn == player1->clientnum) return player1;

        while(cn >= clients.length()) clients.add(NULL);
        if(!clients[cn])
        {
            fpsent *d = new fpsent;
            d->clientnum = cn;
            clients[cn] = d;
            players.add(d);
        }
        return clients[cn];
    }

    fpsent *getclient(int cn)   // ensure valid entity
    {
        if(cn == player1->clientnum) return player1;
        return clients.inrange(cn) ? clients[cn] : NULL;
    }

    void clientdisconnected(int cn, bool notify)
    {
        if(!clients.inrange(cn)) return;
        if(following==cn)
        {
            if(followdir) nextfollow(followdir);
            else stopfollowing();
        }
        unignore(cn);
        fpsent *d = clients[cn];
        if(!d) return;
        if(notify && d->name[0]) conoutf("\f4leave:\f7 %s", colorname(d));
        removeweapons(d);
        removetrackedparticles(d);
        removetrackeddynlights(d);
        if(cmode) cmode->removeplayer(d);
        players.removeobj(d);
        DELETEP(clients[cn]);
        cleardynentcache();
    }

    void clearclients(bool notify)
    {
        loopv(clients) if(clients[i]) clientdisconnected(i, notify);
    }

    void initclient()
    {
        player1 = spawnstate(new fpsent);
        filtertext(player1->name, "unnamed", false, false, MAXNAMELEN);
        players.add(player1);
    }

    VARP(showmodeinfo, 0, 1, 1);

    void startgame()
    {
        clearmovables();
        clearmonsters();

        clearprojectiles();
        clearbouncers();
        clearragdolls();

        clearteaminfo();

        // reset perma-state
        loopv(players)
        {
            fpsent *d = players[i];
            d->frags = d->flags = 0;
            d->deaths = 0;
            d->totaldamage = 0;
            d->totalshots = 0;
            d->maxhealth = 100;
            d->lifesequence = -1;
            d->respawned = d->suicided = -2;
            d->resetextstats();
            if(d->extdatawasinit <= 0) d->extdatawasinit = -1;
        }

        setclientmode();

        intermission = false;
        maptime = maprealtime = 0;
        maplimit = -1;

        if(cmode)
        {
            cmode->preload();
            cmode->setup();
        }

        conoutf(CON_GAMEINFO, "%sgame mode is %s", getmsgcolorstring(), server::modename(gamemode));

        if(m_sp)
        {
            defformatstring(scorename)("bestscore_%s", getclientmap());
            const char *best = getalias(scorename);
            if(*best) conoutf(CON_GAMEINFO, "%stry to beat your best score so far: %s", getmsgcolorstring(), best);
        }
        else
        {
            const char *info = m_valid(gamemode) ? gamemodes[gamemode - STARTGAMEMODE].info : NULL;
            if(showmodeinfo && info) conoutf(CON_GAMEINFO, "\f0%s", info);
        }

        if(player1->playermodel != playermodel) switchplayermodel(playermodel);

        showscores(false);
        disablezoom();
        lasthit = 0;

        if(identexists("mapstart")) execute("mapstart");
    }

    void startmap(const char *name)   // called just after a map load
    {
        ai::savewaypoints();
        ai::clearwaypoints(true);

        respawnent = -1; // so we don't respawn at an old spot
        if(!m_mp(gamemode)) spawnplayer(player1);
        else findplayerspawn(player1, -1);
        entities::resetspawns();
        copystring(clientmap, name ? name : "");
        
        sendmapinfo();
    }

    const char *getmapinfo()
    {
        return showmodeinfo && m_valid(gamemode) ? gamemodes[gamemode - STARTGAMEMODE].info : NULL;
    }

    void physicstrigger(physent *d, bool local, int floorlevel, int waterlevel, int material)
    {
        if(d->type==ENT_INANIMATE) return;
        if     (waterlevel>0) { if(material!=MAT_LAVA) playsound(S_SPLASH1, d==player1 ? NULL : &d->o); }
        else if(waterlevel<0) playsound(material==MAT_LAVA ? S_BURN : S_SPLASH2, d==player1 ? NULL : &d->o);
        if     (floorlevel>0) { if(d==player1 || d->type!=ENT_PLAYER || ((fpsent *)d)->ai) msgsound(S_JUMP, d); }
        else if(floorlevel<0) { if(d==player1 || d->type!=ENT_PLAYER || ((fpsent *)d)->ai) msgsound(S_LAND, d); }
    }

    void dynentcollide(physent *d, physent *o, const vec &dir)
    {
        switch(d->type)
        {
            case ENT_AI: if(dir.z > 0) stackmonster((monster *)d, o); break;
            case ENT_INANIMATE: if(dir.z > 0) stackmovable((movable *)d, o); break;
        }
    }

    void msgsound(int n, physent *d)
    {
        if(!d || d==player1)
        {
            addmsg(N_SOUND, "ci", d, n);
            playsound(n);
        }
        else
        {
            if(d->type==ENT_PLAYER && ((fpsent *)d)->ai)
                addmsg(N_SOUND, "ci", d, n);
            playsound(n, &d->o);
        }
    }

    int numdynents() { return players.length()+monsters.length()+movables.length(); }

    dynent *iterdynents(int i)
    {
        if(i<players.length()) return players[i];
        i -= players.length();
        if(i<monsters.length()) return (dynent *)monsters[i];
        i -= monsters.length();
        if(i<movables.length()) return (dynent *)movables[i];
        return NULL;
    }

    bool duplicatename(fpsent *d, const char *name = NULL, const char *alt = NULL)
    {
        if(!name) name = d->name;
        if(alt && d != player1 && !strcmp(name, alt)) return true;
        loopv(players) if(d!=players[i] && !strcmp(name, players[i]->name)) return true;
        return false;
    }

    static string cname[3];
    static int cidx = 0;

    const char *colorname(fpsent *d, const char *name, const char *prefix, const char *suffix, const char *alt)
    {
        if(!name) name = alt && d == player1 ? alt : d->name; 
        bool dup = !name[0] || duplicatename(d, name, alt) || d->aitype != AI_NONE;
        if(dup || prefix[0] || suffix[0])
        {
            cidx = (cidx+1)%3;
            if(dup) formatstring(cname[cidx])(d->aitype == AI_NONE ? "%s%s \fs\f5(%d)\fr%s" : "%s%s \fs\f5[%d]\fr%s", prefix, name, d->clientnum, suffix);
            else formatstring(cname[cidx])("%s%s%s", prefix, name, suffix);
            return cname[cidx];
        }
        return name;
    }

    VARP(teamcolortext, 0, 1, 1);

    VARP(playerteamcolor, 0, 1, 9);
    XIDENTHOOK(playerteamcolor, IDF_EXTENDED);
    VARP(enemyteamcolor, 0, 3, 9);
    XIDENTHOOK(enemyteamcolor, IDF_EXTENDED);
    VARP(gamemessagescolor, 0, 2, 9);
    XIDENTHOOK(gamemessagescolor, IDF_EXTENDED);

    #define MAX_COLORS 10
    const char* colorstrings[MAX_COLORS] = { "\f0", "\f1", "\f2", "\f3", "\f4",
                                             "\f5", "\f6", "\f7", "\f8", "\f9"};
    const char* getcolorstring(uint n) {
        if(n < MAX_COLORS) {
            return colorstrings[n];
        }
        return colorstrings[7];
    }

    const char* getmsgcolorstring() {
        return colorstrings[gamemessagescolor];
    }

    static const char* getteamcolorstring(bool sameteam) {
        if(!sameteam) {
            return colorstrings[enemyteamcolor];
        } else {
            return colorstrings[playerteamcolor];
        }
    }

    const char *teamcolorname(fpsent *d, const char *alt)
    {
        static char b[10];
        if(!teamcolortext || !m_teammode) return colorname(d, NULL, "", "", alt);
        sprintf(b, "\fs%s", getteamcolorstring(isteam(d->team, getcurrentteam())));
        return colorname(d, NULL, b, "\fr", alt);
    }

    const char *teamcolor(const char *name, bool sameteam, const char *alt)
    {
        if(!teamcolortext || !m_teammode) return sameteam || !alt ? name : alt;
        cidx = (cidx+1)%3;
        formatstring(cname[cidx])("\fs%s%s\fr",
                     getteamcolorstring(sameteam),
                     sameteam || !alt ? name : alt);
        return cname[cidx];
    }    
    
    const char *teamcolor(const char *name, const char *team, const char *alt)
    {
        return teamcolor(name, team && isteam(team, getcurrentteam()), alt);
    }

    void suicide(physent *d)
    {
        if(d==player1 || (d->type==ENT_PLAYER && ((fpsent *)d)->ai))
        {
            if(d->state!=CS_ALIVE) return;
            fpsent *pl = (fpsent *)d;
            if(!m_mp(gamemode)) killed(pl, pl);
            else 
            {
                int seq = (pl->lifesequence<<16)|((lastmillis/1000)&0xFFFF);
                if(pl->suicided!=seq) { addmsg(N_SUICIDE, "rc", pl); pl->suicided = seq; }
            }
        }
        else if(d->type==ENT_AI) suicidemonster((monster *)d);
        else if(d->type==ENT_INANIMATE) suicidemovable((movable *)d);
    }
    ICOMMAND(suicide, "", (), suicide(player1));
    ICOMMAND(kill, "", (), suicide(player1));

    bool needminimap() { return m_ctf || m_protect || m_hold || m_capture || m_collect; }

    void drawicon(int icon, float x, float y, float sz)
    {
        settexture("packages/hud/items.png");
        glBegin(GL_TRIANGLE_STRIP);
        float tsz = 0.25f, tx = tsz*(icon%4), ty = tsz*(icon/4);
        glTexCoord2f(tx,     ty);     glVertex2f(x,    y);
        glTexCoord2f(tx+tsz, ty);     glVertex2f(x+sz, y);
        glTexCoord2f(tx,     ty+tsz); glVertex2f(x,    y+sz);
        glTexCoord2f(tx+tsz, ty+tsz); glVertex2f(x+sz, y+sz);
        glEnd();
    }

    VARP(newhud_abovehud, 800, 925, 1000);
    XIDENTHOOK(newhud_abovehud, IDF_EXTENDED);

    float abovegameplayhud(int w, int h)
    {
        switch(hudplayer()->state)
        {
            case CS_EDITING:
            case CS_SPECTATOR:
                return 1;
            default:
                if(newhud) {
                    return newhud_abovehud/1000.f;
                }
                return 1650.0f/1800.0f;
        }
    }

    int ammohudup[3] = { GUN_CG, GUN_RL, GUN_GL },
        ammohuddown[3] = { GUN_RIFLE, GUN_SG, GUN_PISTOL },
        ammohudcycle[7] = { -1, -1, -1, -1, -1, -1, -1 };

    ICOMMAND(ammohudup, "V", (tagval *args, int numargs),
    {
        loopi(3) ammohudup[i] = i < numargs ? getweapon(args[i].getstr()) : -1;
    });

    ICOMMAND(ammohuddown, "V", (tagval *args, int numargs),
    {
        loopi(3) ammohuddown[i] = i < numargs ? getweapon(args[i].getstr()) : -1;
    });

    ICOMMAND(ammohudcycle, "V", (tagval *args, int numargs),
    {
        loopi(7) ammohudcycle[i] = i < numargs ? getweapon(args[i].getstr()) : -1;
    });

    VARP(ammobar, 0, 0, 1);
    XIDENTHOOK(ammobar, IDF_EXTENDED);
    VARP(ammobardisablewithgui, 0, 0, 1);
    XIDENTHOOK(ammobardisablewithgui, IDF_EXTENDED);
    VARP(ammobardisableininsta, 0, 0, 1);
    XIDENTHOOK(ammobardisableininsta, IDF_EXTENDED);

    VARP(ammobarfilterempty, 0, 0, 1);
    XIDENTHOOK(ammobarfilterempty, IDF_EXTENDED);
    VARP(ammobariconspos, -1, -1, 1);
    XIDENTHOOK(ammobariconspos, IDF_EXTENDED);

    VARP(ammobarsize, 1, 5, 30);
    XIDENTHOOK(ammobarsize, IDF_EXTENDED);
    VARP(ammobaroffset_x, 0, 10, 1000);
    XIDENTHOOK(ammobaroffset_x, IDF_EXTENDED);

    VARP(ammobaroffset_start_x, -1, -1, 1);
    XIDENTHOOK(ammobaroffset_start_x, IDF_EXTENDED);

    VARP(ammobaroffset_y, 0, 500, 1000);
    XIDENTHOOK(ammobaroffset_y, IDF_EXTENDED);
    VARP(ammobarhorizontal, 0, 0, 1);
    XIDENTHOOK(ammobarhorizontal, IDF_EXTENDED);

    VARP(ammobarselectedcolor_r, 0, 100, 255);
    XIDENTHOOK(ammobarselectedcolor_r, IDF_EXTENDED);
    VARP(ammobarselectedcolor_g, 0, 200, 255);
    XIDENTHOOK(ammobarselectedcolor_g, IDF_EXTENDED);
    VARP(ammobarselectedcolor_b, 0, 255, 255);
    XIDENTHOOK(ammobarselectedcolor_b, IDF_EXTENDED);
    VARP(ammobarselectedcolor_a, 0, 150, 255);
    XIDENTHOOK(ammobarselectedcolor_a, IDF_EXTENDED);

    VARP(coloredammo, 0, 0, 1);
    XIDENTHOOK(coloredammo, IDF_EXTENDED);

    void getammocolor(fpsent *d, int gun, int &r, int &g, int &b, int &a) {
        if(!d) return;
        if(gun == 0) {
            r = 255, g = 255, b = 255, a = 255;
        } else if(gun == 2 || gun == 6) {
            if(d->ammo[gun] > 10) {
                r = 255, g = 255, b = 255, a = 255;
            } else if(d->ammo[gun] > 5) {
                r = 255, g = 127, b = 0, a = 255;
            } else {
                r = 255, g = 0, b = 0, a = 255;
            }
        } else {
            if(d->ammo[gun] > 4) {
                r = 255, g = 255, b = 255, a = 255;
            } else if(d->ammo[gun] > 2) {
                r = 255, g = 127, b = 0, a = 255;
            } else {
                r = 255, g = 0, b = 0, a = 255;
            }
        }
    }

    static void drawselectedammobg(float x, float y, float w, float h) {
        drawacoloredquad(x, y, w, h,
                         (GLubyte)ammobarselectedcolor_r,
                         (GLubyte)ammobarselectedcolor_g,
                         (GLubyte)ammobarselectedcolor_b,
                         (GLubyte)ammobarselectedcolor_a);
    }

    void drawammobar(fpsent *d, int w, int h) {
        if(!d) return;
        #define NWEAPONS 6
        float conw = w/staticscale, conh = h/staticscale;

        int icons[NWEAPONS] = {GUN_SG, GUN_CG, GUN_RL, GUN_RIFLE, GUN_GL, GUN_PISTOL};

        int r = 255, g = 255, b = 255, a = 255;
        char buff[10];
        float ammobarscale = (1 + ammobarsize/10.0)*h/1080.0;
        float xoff = 0.0;
        float yoff = ammobaroffset_y*conh/1000;
        float vsep = 15;
        float hsep = 15;
        float hgap = ammobariconspos == 0 ? 40 : 60;
        float vgap = ammobariconspos == 0 ? 30 : 20;
        float textsep = 10;
        int pw = 0, ph = 0, tw = 0, th = 0;

        glPushMatrix();
        glScalef(staticscale*ammobarscale, staticscale*ammobarscale, 1);
        draw_text("", 0, 0, 255, 255, 255, 255);

        int szx = 0, szy = 0, eszx = 0, eszy = 0;
        text_bounds("999", pw, ph);

        if(ammobariconspos == 0) {
            eszx = pw;
            eszy = 2*ph + textsep;
        } else {
            eszx = ph + textsep + pw;
            eszy = ph;
        }

        if(ammobarhorizontal) {
            szx = NWEAPONS * (eszx + hgap) - hgap + hsep;
            szy = eszy + vsep;
        } else {
            szx = eszx + hsep;
            szy = NWEAPONS * (eszy + vgap) - vgap + vsep;
        }

        if(ammobaroffset_start_x == 1) {
            xoff = (1000-ammobaroffset_x)*conw/1000 - szx * ammobarscale;
        } else if(ammobaroffset_start_x == 0) {
            xoff = ammobaroffset_x*conw/1000 - szx/2.0 * ammobarscale;
        } else {
            xoff = ammobaroffset_x*conw/1000;
        }
        yoff -= szy/2.0 * ammobarscale;

        for(int i = 0, xpos = 0, ypos = 0; i < NWEAPONS; i++) {
            snprintf(buff, 10, "%d", limitammo(d->ammo[i+1]));
            text_bounds(buff, tw, th);
            draw_text("", 0, 0, 255, 255, 255, 255);
            if(i+1 == d->gunselect) {
                drawselectedammobg(xoff/ammobarscale + xpos,
                                   yoff/ammobarscale + ypos,
                                   eszx + hsep,
                                   eszy + vsep);
            }
            if(ammobarfilterempty && d->ammo[i+1] == 0) {
                draw_text("", 0, 0, 255, 255, 255, 85);
            }
            if(ammobariconspos == -1) {
                drawicon(HICON_FIST+icons[i], xoff/ammobarscale + xpos + hsep/2.0, yoff/ammobarscale + ypos + vsep/2.0, ph);
            } else if(ammobariconspos == 1) {
                drawicon(HICON_FIST+icons[i], xoff/ammobarscale + xpos + eszx - ph - hsep/2.0, yoff/ammobarscale + ypos + vsep/2.0, ph);
            } else {
                drawicon(HICON_FIST+icons[i], xoff/ammobarscale + xpos + (pw-th)/2.0 + hsep/2.0, yoff/ammobarscale + ypos + 0.75*vsep, ph);
            }
            if(coloredammo) getammocolor(d, i+1, r, g, b, a);
            if( !(ammobarfilterempty && d->ammo[i+1] == 0) ) {
                if(ammobariconspos == -1) {
                    draw_text(buff, xoff/ammobarscale + xpos + hsep/2.0 + ph + textsep + (pw-tw)/2.0,
                              yoff/ammobarscale + ypos + vsep/2.0, r, g, b, a);
                } else if(ammobariconspos == 1) {
                    draw_text(buff, xoff/ammobarscale + xpos + hsep/2.0 + (pw-tw)/2.0,
                              yoff/ammobarscale + ypos + vsep/2.0, r, g, b, a);
                } else {
                    draw_text(buff, xoff/ammobarscale + xpos + hsep/2.0 + (pw-tw)/2.0,
                              yoff/ammobarscale + ypos + 0.75*vsep + textsep + ph, r, g, b, a);
                }
            }
            if(ammobarhorizontal) {
                xpos += eszx + hgap;
            } else {
                ypos += eszy + vgap;
            }
        }
        draw_text("", 0, 0, 255, 255, 255, 255);
        glPopMatrix();
        #undef NWEAPONS
    }

    VARP(ammohud, 0, 1, 1);

    void drawammohud(fpsent *d)
    {
        float x = HICON_X + 2*HICON_STEP, y = HICON_Y, sz = HICON_SIZE;
        glPushMatrix();
        glScalef(1/3.2f, 1/3.2f, 1);
        float xup = (x+sz)*3.2f, yup = y*3.2f + 0.1f*sz;
        loopi(3)
        {
            int gun = ammohudup[i];
            if(gun < GUN_FIST || gun > GUN_PISTOL || gun == d->gunselect || !d->ammo[gun]) continue;
            drawicon(HICON_FIST+gun, xup, yup, sz);
            yup += sz;
        }
        float xdown = x*3.2f - sz, ydown = (y+sz)*3.2f - 0.1f*sz;
        loopi(3)
        {
            int gun = ammohuddown[3-i-1];
            if(gun < GUN_FIST || gun > GUN_PISTOL || gun == d->gunselect || !d->ammo[gun]) continue;
            ydown -= sz;
            drawicon(HICON_FIST+gun, xdown, ydown, sz);
        }
        int offset = 0, num = 0;
        loopi(7)
        {
            int gun = ammohudcycle[i];
            if(gun < GUN_FIST || gun > GUN_PISTOL) continue;
            if(gun == d->gunselect) offset = i + 1;
            else if(d->ammo[gun]) num++;
        }
        float xcycle = (x+sz/2)*3.2f + 0.5f*num*sz, ycycle = y*3.2f-sz;
        loopi(7)
        {
            int gun = ammohudcycle[(i + offset)%7];
            if(gun < GUN_FIST || gun > GUN_PISTOL || gun == d->gunselect || !d->ammo[gun]) continue;
            xcycle -= sz;
            drawicon(HICON_FIST+gun, xcycle, ycycle, sz);
        }
        glPopMatrix();
    }

    int composedhealth(fpsent *d) {
        if(d->armour) {
            double absorbk = (d->armourtype+1)*0.25;
            int d1 = d->health/(1.0 - absorbk);
            int d2 = d->health + d->armour;
            if(d1 < d->armour/absorbk) {
                return d1; // more armor than health
            } else {
                return d2; // more health than armor
            }
        } else {
            return d->health;
        }
    }

    VARP(coloredhealth, 0, 0, 1);
    XIDENTHOOK(coloredhealth, IDF_EXTENDED);

    void getchpcolors(fpsent *d, int& r, int& g, int& b, int& a) {
        int chp = d->state==CS_DEAD ? 0 : composedhealth(d);
        if(chp > 250) {
            r = 0, g = 127, b = 255, a = 255;
        } else if(chp > 200) {
            r = 0, g = 255, b = 255, a = 255;
        } else if(chp > 150) {
            r = 0, g = 255, b = 127, a = 255;
        } else if(chp > 100) {
            r = 127, g = 255, b = 0, a = 255;
        } else if(chp > 50) {
            r = 255, g = 127, b = 0, a = 255;
        } else {
            r = 255, g = 0, b = 0, a = 255;
        }
    }

    void drawhudicons(fpsent *d, int w, int h)
    {
        glPushMatrix();
        glScalef(h/1800.0f, h/1800.0f, 1);

        drawicon(HICON_HEALTH, HICON_X, HICON_Y);

        glPushMatrix();
        glScalef(2, 2, 1);

        char buff[10];
        int r = 255, g = 255, b = 255, a = 255;
        if(coloredhealth && !m_insta) getchpcolors(d, r, g, b, a);
        snprintf(buff, 10, "%d", d->state==CS_DEAD ? 0 : d->health);
        draw_text(buff, (HICON_X + HICON_SIZE + HICON_SPACE)/2, HICON_TEXTY/2, r, g, b, a);
        if(d->state!=CS_DEAD)
        {
            if(d->armour) {
                snprintf(buff, 10, "%d", d->armour);
                draw_text(buff, (HICON_X + HICON_STEP + HICON_SIZE + HICON_SPACE)/2, HICON_TEXTY/2, r, g, b, a);
            }
            r = 255, g = 255, b = 255, a = 255;
            snprintf(buff, 10, "%d", d->ammo[d->gunselect]);
            if(coloredammo && !m_insta) getammocolor(d, d->gunselect, r, g, b, a);
            draw_text(buff, (HICON_X + 2*HICON_STEP + HICON_SIZE + HICON_SPACE)/2, HICON_TEXTY/2, r, g, b, a);
        }
        draw_text("", 0, 0, 255, 255, 255, 255);
        glPopMatrix();

        if(d->state!=CS_DEAD)
        {
            if(d->armour) drawicon(HICON_BLUE_ARMOUR+d->armourtype, HICON_X + HICON_STEP, HICON_Y);
            drawicon(HICON_FIST+d->gunselect, HICON_X + 2*HICON_STEP, HICON_Y);
            if(d->quadmillis) drawicon(HICON_QUAD, HICON_X + 3*HICON_STEP, HICON_Y);
            if(ammohud) drawammohud(d);
        }
        glPopMatrix();
    }

    VARP(newhud_hpdisableininsta, 0, 0, 1);
    XIDENTHOOK(newhud_hpdisableininsta, IDF_EXTENDED);
    VARP(newhud_hpdisablewithgui, 0, 0, 1);
    XIDENTHOOK(newhud_hpdisablewithgui, IDF_EXTENDED);

    VARP(newhud_hpssize, 0, 30, 50);
    XIDENTHOOK(newhud_hpssize, IDF_EXTENDED);
    VARP(newhud_hpgap, 0, 100, 200);
    XIDENTHOOK(newhud_hpgap, IDF_EXTENDED);
    VARP(newhud_hpiconssize, 0, 60, 200);
    XIDENTHOOK(newhud_hpiconssize, IDF_EXTENDED);
    VARP(newhud_hppos_x, 0, 420, 1000);
    XIDENTHOOK(newhud_hppos_x, IDF_EXTENDED);
    VARP(newhud_hppos_y, 0, 960, 1000);
    XIDENTHOOK(newhud_hppos_y, IDF_EXTENDED);

    void drawnewhudhp(fpsent *d, int w, int h) {
        if((m_insta && newhud_hpdisableininsta) ||
           (newhud_hpdisablewithgui && framehasgui))
            return;

        int conw = int(w/staticscale), conh = int(h/staticscale);
        float hpscale = (1 + newhud_hpssize/10.0)*h/1080.0;
        float xoff = newhud_hppos_x*conw/1000;
        float yoff = newhud_hppos_y*conh/1000;

        glPushMatrix();
        glScalef(staticscale*hpscale, staticscale*hpscale, 1);
        draw_text("", 0, 0, 255, 255, 255, 255);

        char buff[10];
        int r = 255, g = 255, b = 255, a = 255, tw = 0, th = 0;
        float hsep = 20.0*((float)newhud_hpgap/100.0f);
        if(coloredhealth && !m_insta) getchpcolors(d, r, g, b, a);
        snprintf(buff, 10, "%d", d->state==CS_DEAD ? 0 : d->health);
        text_bounds(buff, tw, th);

        float iconsz = th*newhud_hpiconssize/100.0;
        xoff -= iconsz/2.0*hpscale;
        draw_text(buff, xoff/hpscale - tw - hsep, yoff/hpscale - th/2.0, r, g, b, a);
        if(d->state!=CS_DEAD && d->armour) {
            draw_text("", 0, 0, 255, 255, 255, 255);
            drawicon(HICON_BLUE_ARMOUR+d->armourtype, xoff/hpscale, yoff/hpscale - iconsz/2.0, iconsz);
            snprintf(buff, 10, "%d", d->armour);
            draw_text(buff, xoff/hpscale + iconsz + hsep, yoff/hpscale - th/2.0, r, g, b, a);
        }
        draw_text("", 0, 0, 255, 255, 255, 255);

        glPopMatrix();
    }

    VARP(newhud_ammodisable, 0, 0, 1);
    XIDENTHOOK(newhud_ammodisable, IDF_EXTENDED);
    VARP(newhud_ammodisableininsta, -1, 0, 1);
    XIDENTHOOK(newhud_ammodisableininsta, IDF_EXTENDED);
    VARP(newhud_ammodisablewithgui, 0, 0, 1);
    XIDENTHOOK(newhud_ammodisablewithgui, IDF_EXTENDED);

    VARP(newhud_ammosize, 0, 30, 50);
    XIDENTHOOK(newhud_ammosize, IDF_EXTENDED);
    VARP(newhud_ammoiconssize, 0, 60, 200);
    XIDENTHOOK(newhud_ammoiconssize, IDF_EXTENDED);
    VARP(newhud_ammogap, 0, 100, 200);
    XIDENTHOOK(newhud_ammogap, IDF_EXTENDED);
    VARP(newhud_ammopos_x, 0, 580, 1000);
    XIDENTHOOK(newhud_ammopos_x, IDF_EXTENDED);
    VARP(newhud_ammopos_y, 0, 960, 1000);
    XIDENTHOOK(newhud_ammopos_y, IDF_EXTENDED);

    void drawnewhudammo(fpsent *d, int w, int h) {
        if(d->state==CS_DEAD || newhud_ammodisable ||
           (m_insta && newhud_ammodisableininsta == 1) ||
           (!m_insta && newhud_ammodisableininsta == -1) ||
           (newhud_ammodisablewithgui && framehasgui))
            return;

        int conw = int(w/staticscale), conh = int(h/staticscale);
        float ammoscale = (1 + newhud_ammosize/10.0)*h/1080.0;
        float xoff = newhud_ammopos_x*conw/1000;
        float yoff = newhud_ammopos_y*conh/1000;
        float hsep = 20.0*((float)newhud_ammogap/100.0f);
        int r = 255, g = 255, b = 255, a = 255, tw = 0, th = 0;

        glPushMatrix();
        glScalef(staticscale*ammoscale, staticscale*ammoscale, 1);
        draw_text("", 0, 0, 255, 255, 255, 255);

        char buff[10];
        snprintf(buff, 10, "%d", d->ammo[d->gunselect]);
        text_bounds(buff, tw, th);
        float iconsz = th*newhud_ammoiconssize/100.0;
        xoff -= iconsz/2.0*ammoscale;

        drawicon(HICON_FIST+d->gunselect, xoff/ammoscale, yoff/ammoscale - iconsz/2.0, iconsz);

        if(coloredammo && !m_insta) getammocolor(d, d->gunselect, r, g, b, a);
        draw_text(buff, xoff/ammoscale + hsep + iconsz, yoff/ammoscale - th/2.0, r, g, b, a);
        draw_text("", 0, 0, 255, 255, 255, 255);

        glPopMatrix();
    }

    VARP(gameclock, 0, 0, 1);
    XIDENTHOOK(gameclock, IDF_EXTENDED);
    VARP(gameclockdisablewithgui, 0, 0, 1);
    XIDENTHOOK(gameclockdisablewithgui, IDF_EXTENDED);
    VARP(gameclocksize, 1, 5, 30);
    XIDENTHOOK(gameclocksize, IDF_EXTENDED);

    VARP(gameclockoffset_x, 0, 10, 1000);
    XIDENTHOOK(gameclockoffset_x, IDF_EXTENDED);
    VARP(gameclockoffset_start_x, -1, 1, 1);
    XIDENTHOOK(gameclockoffset_start_x, IDF_EXTENDED);
    VARP(gameclockoffset_y, 0, 300, 1000);
    XIDENTHOOK(gameclockoffset_y, IDF_EXTENDED);

    VARP(gameclockcolor_r, 0, 255, 255);
    XIDENTHOOK(gameclockcolor_r, IDF_EXTENDED);
    VARP(gameclockcolor_g, 0, 255, 255);
    XIDENTHOOK(gameclockcolor_g, IDF_EXTENDED);
    VARP(gameclockcolor_b, 0, 255, 255);
    XIDENTHOOK(gameclockcolor_b, IDF_EXTENDED);
    VARP(gameclockcolor_a, 0, 255, 255);
    XIDENTHOOK(gameclockcolor_a, IDF_EXTENDED);

    VARP(gameclockcolorbg_r, 0, 100, 255);
    XIDENTHOOK(gameclockcolorbg_r, IDF_EXTENDED);
    VARP(gameclockcolorbg_g, 0, 200, 255);
    XIDENTHOOK(gameclockcolorbg_g, IDF_EXTENDED);
    VARP(gameclockcolorbg_b, 0, 255, 255);
    XIDENTHOOK(gameclockcolorbg_b, IDF_EXTENDED);
    VARP(gameclockcolorbg_a, 0, 50, 255);
    XIDENTHOOK(gameclockcolorbg_a, IDF_EXTENDED);


    void drawclock(int w, int h) {
        int conw = int(w/staticscale), conh = int(h/staticscale);

        char buf[10];
        int millis = max(game::maplimit-lastmillis, 0);
        int secs = millis/1000;
        int mins = secs/60;
        secs %= 60;
        snprintf(buf, 10, "%d:%02d", mins, secs);

        int r = gameclockcolor_r,
            g = gameclockcolor_g,
            b = gameclockcolor_b,
            a = gameclockcolor_a;
        float gameclockscale = (1 + gameclocksize/10.0)*h/1080.0;

        glPushMatrix();
        glScalef(staticscale*gameclockscale, staticscale*gameclockscale, 1);
        draw_text("", 0, 0, 255, 255, 255, 255);

        int tw = 0, th = 0;
        float xoff = 0.0, xpos = 0.0, ypos = 0.0;
        float yoff = gameclockoffset_y*conh/1000;
        float borderx = 10.0;
        text_bounds(buf, tw, th);

        if(gameclockoffset_start_x == 1) {
            xoff = (1000 - gameclockoffset_x)*conw/1000;
            xpos = xoff/gameclockscale - tw - 2.0 * borderx;
        } else if(gameclockoffset_start_x == 0) {
            xoff = gameclockoffset_x*conw/1000;
            xpos = xoff/gameclockscale - tw/2.0;
        } else {
            xoff = gameclockoffset_x*conw/1000;
            xpos = xoff/gameclockscale;
        }
        ypos = yoff/gameclockscale - th/2.0;

        drawacoloredquad(xpos,
                         ypos,
                         tw + 2.0*borderx,
                         th,
                         (GLubyte)gameclockcolorbg_r,
                         (GLubyte)gameclockcolorbg_g,
                         (GLubyte)gameclockcolorbg_b,
                         (GLubyte)gameclockcolorbg_a);
        draw_text(buf, xpos + borderx, ypos, r, g, b, a);
        draw_text("", 0, 0, 255, 255, 255, 255);

        glPopMatrix();
    }

    VARP(hudscores, 0, 0, 1);
    XIDENTHOOK(hudscores, IDF_EXTENDED);

    VARP(hudscoresdisablewithgui, 0, 0, 1);
    XIDENTHOOK(hudscoresdisablewithgui, IDF_EXTENDED);

    VARP(hudscoresvertical, 0, 0, 1);
    XIDENTHOOK(hudscoresvertical, IDF_EXTENDED);

    VARP(hudscoressize, 1, 5, 30);
    XIDENTHOOK(hudscoressize, IDF_EXTENDED);

    VARP(hudscoresoffset_x, 0, 10, 1000);
    XIDENTHOOK(hudscoresoffset_x, IDF_EXTENDED);
    VARP(hudscoresoffset_start_x, -1, 1, 1);
    XIDENTHOOK(hudscoresoffset_start_x, IDF_EXTENDED);
    VARP(hudscoresoffset_y, 0, 350, 1000);
    XIDENTHOOK(hudscoresoffset_y, IDF_EXTENDED);

    VARP(hudscoresplayercolor_r, 0, 0, 255);
    XIDENTHOOK(hudscoresplayercolor_r, IDF_EXTENDED);
    VARP(hudscoresplayercolor_g, 0, 255, 255);
    XIDENTHOOK(hudscoresplayercolor_g, IDF_EXTENDED);
    VARP(hudscoresplayercolor_b, 0, 255, 255);
    XIDENTHOOK(hudscoresplayercolor_b, IDF_EXTENDED);
    VARP(hudscoresplayercolor_a, 0, 255, 255);
    XIDENTHOOK(hudscoresplayercolor_a, IDF_EXTENDED);

    VARP(hudscoresplayercolorbg_r, 0, 0, 255);
    XIDENTHOOK(hudscoresplayercolorbg_r, IDF_EXTENDED);
    VARP(hudscoresplayercolorbg_g, 0, 255, 255);
    XIDENTHOOK(hudscoresplayercolorbg_g, IDF_EXTENDED);
    VARP(hudscoresplayercolorbg_b, 0, 255, 255);
    XIDENTHOOK(hudscoresplayercolorbg_b, IDF_EXTENDED);
    VARP(hudscoresplayercolorbg_a, 0, 50, 255);
    XIDENTHOOK(hudscoresplayercolorbg_a, IDF_EXTENDED);

    VARP(hudscoresenemycolor_r, 0, 255, 255);
    XIDENTHOOK(hudscoresenemycolor_r, IDF_EXTENDED);
    VARP(hudscoresenemycolor_g, 0, 0, 255);
    XIDENTHOOK(hudscoresenemycolor_g, IDF_EXTENDED);
    VARP(hudscoresenemycolor_b, 0, 0, 255);
    XIDENTHOOK(hudscoresenemycolor_b, IDF_EXTENDED);
    VARP(hudscoresenemycolor_a, 0, 255, 255);
    XIDENTHOOK(hudscoresenemycolor_a, IDF_EXTENDED);

    VARP(hudscoresenemycolorbg_r, 0, 255, 255);
    XIDENTHOOK(hudscoresenemycolorbg_r, IDF_EXTENDED);
    VARP(hudscoresenemycolorbg_g, 0, 85, 255);
    XIDENTHOOK(hudscoresenemycolorbg_g, IDF_EXTENDED);
    VARP(hudscoresenemycolorbg_b, 0, 85, 255);
    XIDENTHOOK(hudscoresenemycolorbg_b, IDF_EXTENDED);
    VARP(hudscoresenemycolorbg_a, 0, 50, 255);
    XIDENTHOOK(hudscoresenemycolorbg_a, IDF_EXTENDED);

    void drawscores(int w, int h) {
        int conw = int(w/staticscale), conh = int(h/staticscale);

        vector<fpsent *> bestplayers;
        vector<scoregroup *> bestgroups;
        int grsz = 0;

        if(m_teammode) { grsz = groupplayers(); bestgroups = getscoregroups(); }
        else { getbestplayers(bestplayers,1); grsz = bestplayers.length(); }

        float scorescale = (1 + hudscoressize/10.0)*h/1080.0;
        float xoff = hudscoresoffset_start_x == 1 ? (1000 - hudscoresoffset_x)*conw/1000 : hudscoresoffset_x*conw/1000;
        float yoff = hudscoresoffset_y*conh/1000;
        float scoresep = hudscoresvertical ? 30 : 40;
        float borderx = scoresep/2.0;

        int r1, g1, b1, a1, r2, g2, b2, a2,
            bgr1, bgg1, bgb1, bga1, bgr2, bgg2, bgb2, bga2;
        int tw1=0, th1=0, tw2=0, th2=0;

        if(grsz) {
            char buff1[5], buff2[5];
            int isbest=1;
            fpsent* currentplayer = (player1->state == CS_SPECTATOR) ? followingplayer() : player1;
            if(!currentplayer) return;

            if(m_teammode) isbest = ! strcmp(currentplayer->team, bestgroups[0]->team);
            else isbest = currentplayer == bestplayers[0];

            glPushMatrix();
            glScalef(staticscale*scorescale, staticscale*scorescale, 1);
            draw_text("", 0, 0, 255, 255, 255, 255);

            if(isbest) {
                int frags=0, frags2=0;
                if(m_teammode) frags = bestgroups[0]->score;
                else frags = bestplayers[0]->frags;
                frags = limitscore(frags);

                if(frags >= 9999) {
                    snprintf(buff1, 5, "WIN");
                } else {
                    snprintf(buff1, 5, "%d", frags);
                }
                text_bounds(buff1, tw1, th1);

                if(grsz > 1) {
                    if(m_teammode) frags2 = bestgroups[1]->score;
                    else frags2 = bestplayers[1]->frags;
                    frags2 = limitscore(frags2);

                    if(frags2 >= 9999) {
                        snprintf(buff2, 5, "WIN");
                    } else {
                        snprintf(buff2, 5, "%d", frags2);
                    }
                    text_bounds(buff2, tw2, th2);
                } else {
                    snprintf(buff2, 5, " ");
                    text_bounds(buff2, tw2, th2);
                }

                r1 = hudscoresplayercolor_r;
                g1 = hudscoresplayercolor_g;
                b1 = hudscoresplayercolor_b;
                a1 = hudscoresplayercolor_a;

                r2 = hudscoresenemycolor_r;
                g2 = hudscoresenemycolor_g;
                b2 = hudscoresenemycolor_b;
                a2 = hudscoresenemycolor_a;

                bgr1 = hudscoresplayercolorbg_r;
                bgg1 = hudscoresplayercolorbg_g;
                bgb1 = hudscoresplayercolorbg_b;
                bga1 = hudscoresplayercolorbg_a;

                bgr2 = hudscoresenemycolorbg_r;
                bgg2 = hudscoresenemycolorbg_g;
                bgb2 = hudscoresenemycolorbg_b;
                bga2 = hudscoresenemycolorbg_a;
            } else {
                int frags=0, frags2=0;
                if(m_teammode) frags = bestgroups[0]->score;
                else frags = bestplayers[0]->frags;
                frags = limitscore(frags);

                if(frags >= 9999) {
                    snprintf(buff1, 5, "WIN");
                } else {
                    snprintf(buff1, 5, "%d", frags);
                }
                text_bounds(buff1, tw1, th1);

                if(m_teammode) {
                    loopk(grsz) {
                        if( ! strcmp(bestgroups[k]->team, currentplayer->team))
                            frags2 = bestgroups[k]->score;
                    }
                } else {
                    frags2 = currentplayer->frags;
                }
                frags2 = limitscore(frags2);

                if(frags2 >= 9999) {
                    snprintf(buff2, 5, "WIN");
                } else {
                    snprintf(buff2, 5, "%d", frags2);
                }
                text_bounds(buff2, tw2, th2);

                r2 = hudscoresplayercolor_r;
                g2 = hudscoresplayercolor_g;
                b2 = hudscoresplayercolor_b;
                a2 = hudscoresplayercolor_a;

                r1 = hudscoresenemycolor_r;
                g1 = hudscoresenemycolor_g;
                b1 = hudscoresenemycolor_b;
                a1 = hudscoresenemycolor_a;

                bgr2 = hudscoresplayercolorbg_r;
                bgg2 = hudscoresplayercolorbg_g;
                bgb2 = hudscoresplayercolorbg_b;
                bga2 = hudscoresplayercolorbg_a;

                bgr1 = hudscoresenemycolorbg_r;
                bgg1 = hudscoresenemycolorbg_g;
                bgb1 = hudscoresenemycolorbg_b;
                bga1 = hudscoresenemycolorbg_a;
            }
            int fw = 0, fh = 0;
            text_bounds("00", fw, fh);
            fw = max(fw, max(tw1, tw2));

            float addoffset = 0.0, offsety = 0.0;
            if(hudscoresoffset_start_x == 1) {
                addoffset = 2.0 * fw + 2.0 * borderx + scoresep;
            } else if(hudscoresoffset_start_x == 0) {
                addoffset = (2.0 * fw + 2.0 * borderx + scoresep)/2.0;
            }

            if(hudscoresvertical) {
                addoffset /= 2.0;
                offsety = -fh;
            } else {
                offsety = -fh/2.0;
            }

            xoff -= addoffset*scorescale;

            drawacoloredquad(xoff/scorescale,
                             yoff/scorescale + offsety,
                             fw + 2.0*borderx,
                             th1,
                             (GLubyte)bgr1,
                             (GLubyte)bgg1,
                             (GLubyte)bgb1,
                             (GLubyte)bga1);
            draw_text(buff1, xoff/scorescale + borderx + (fw-tw1)/2.0,
                      yoff/scorescale + offsety, r1, g1, b1, a1);

            if(!hudscoresvertical) {
                xoff += (fw + scoresep)*scorescale;
            } else {
                offsety = 0;
            }

            drawacoloredquad(xoff/scorescale,
                             yoff/scorescale + offsety,
                             fw + 2.0*borderx,
                             th2,
                             (GLubyte)bgr2,
                             (GLubyte)bgg2,
                             (GLubyte)bgb2,
                             (GLubyte)bga2);
            draw_text(buff2, xoff/scorescale + borderx + (fw-tw2)/2.0,
                      yoff/scorescale + offsety, r2, g2, b2, a2);

            draw_text("", 0, 0, 255, 255, 255, 255);
            glPopMatrix();
        }
    }

    VARP(newhud_spectatorsdisablewithgui, 0, 1, 1);
    XIDENTHOOK(newhud_spectatorsdisablewithgui, IDF_EXTENDED);
    VARP(newhud_spectatorsnocolor, 0, 1, 1);
    XIDENTHOOK(newhud_spectatorsnocolor, IDF_EXTENDED);
    VARP(newhud_spectatorsize, 0, 5, 30);
    XIDENTHOOK(newhud_spectatorsize, IDF_EXTENDED);
    VARP(newhud_spectatorpos_x, 0, 500, 1000);
    XIDENTHOOK(newhud_spectatorpos_x, IDF_EXTENDED);
    VARP(newhud_spectatorpos_start_x, -1, 0, 1);
    XIDENTHOOK(newhud_spectatorpos_start_x, IDF_EXTENDED);
    VARP(newhud_spectatorpos_y, 0, 110, 1000);
    XIDENTHOOK(newhud_spectatorpos_y, IDF_EXTENDED);

    void drawspectator(int w, int h) {
        fpsent *f = followingplayer();
        if(!f || player1->state!=CS_SPECTATOR) return;
 
        int conw = int(w/staticscale), conh = int(h/staticscale);
        float specscale = (1 + newhud_spectatorsize/10.0)*h/1080.0;
        float xoff = newhud_spectatorpos_x*conw/1000;
        float yoff = newhud_spectatorpos_y*conh/1000;

        glPushMatrix();
        if(newhud) {
            glScalef(staticscale*specscale, staticscale*specscale, 1);
        } else {
            glScalef(h/1800.0f, h/1800.0f, 1);
        }

        draw_text("", 0, 0, 255, 255, 255, 255);
        int pw, ph, tw, th, fw, fh;
        text_bounds("  ", pw, ph);
        text_bounds("SPECTATOR", tw, th);
        th = max(th, ph);
        text_bounds(f ? colorname(f) : " ", fw, fh);
        fh = max(fh, ph);
        if(!newhud) {
            draw_text("SPECTATOR", w*1800/h - tw - pw, 1650 - th - fh);
        }
        if(f) {
            int color = f->state!=CS_DEAD ? 0xFFFFFF : 0x606060;
            if(f->privilege) {
                if(!newhud || !newhud_spectatorsnocolor) {
                    color = f->privilege>=PRIV_ADMIN ? 0xFF8000 : 0x40FF80;
                    if(f->state==CS_DEAD) color = (color>>1)&0x7F7F7F;
                }
            }
            if(newhud) {
                const char *cname;
                int w1=0, h1=0;
                cname = colorname(f);
                text_bounds(cname, w1, h1);
                if(newhud_spectatorpos_start_x == 0) {
                    draw_text(cname, xoff/specscale - w1/2.0, yoff/specscale,
                              (color>>16)&0xFF, (color>>8)&0xFF, color&0xFF);
                } else if(newhud_spectatorpos_start_x == 1) {
                    xoff = (1000 - newhud_spectatorpos_x)*conw/1000;
                    draw_text(cname, xoff/specscale - w1, yoff/specscale,
                              (color>>16)&0xFF, (color>>8)&0xFF, color&0xFF);
                } else {
                    draw_text(cname, xoff/specscale, yoff/specscale,
                              (color>>16)&0xFF, (color>>8)&0xFF, color&0xFF);
                }
            } else {
                draw_text(colorname(f), w*1800/h - fw - pw, 1650 - fh, (color>>16)&0xFF, (color>>8)&0xFF, color&0xFF);
            }
        }
        draw_text("", 0, 0, 255, 255, 255, 255);
        glPopMatrix();
    }

    VARP(newhud_itemsdisablewithgui, 0, 0, 1);
    XIDENTHOOK(newhud_itemsdisablewithgui, IDF_EXTENDED);
    VARP(newhud_itemssize, 0, 20, 50);
    XIDENTHOOK(newhud_itemssize, IDF_EXTENDED);
    VARP(newhud_itemspos_x, 0, 10, 1000);
    XIDENTHOOK(newhud_itemspos_x, IDF_EXTENDED);
    VARP(newhud_itemspos_reverse_x, 0, 1, 1);
    XIDENTHOOK(newhud_itemspos_reverse_x, IDF_EXTENDED);
    VARP(newhud_itemspos_centerfirst, 0, 0, 1);
    XIDENTHOOK(newhud_itemspos_centerfirst, IDF_EXTENDED);
    VARP(newhud_itemspos_y, 0, 920, 1000);
    XIDENTHOOK(newhud_itemspos_y, IDF_EXTENDED);

    void drawnewhuditems(fpsent *d, int w, int h) {
        if(newhud_itemsdisablewithgui && framehasgui) return;
        if(d->quadmillis) {
            char buff[10];
            int conw = int(w/staticscale), conh = int(h/staticscale);
            float itemsscale = (1 + newhud_itemssize/10.0)*h/1080.0;
            float xoff = newhud_itemspos_reverse_x ? (1000 - newhud_itemspos_x)*conw/1000 : newhud_itemspos_x*conw/1000;
            float yoff = newhud_itemspos_y*conh/1000;

            glPushMatrix();
            snprintf(buff, 10, "M");
            int tw = 0, th = 0, adj = 0;
            text_bounds(buff, tw, th);
            glScalef(staticscale*itemsscale, staticscale*itemsscale, 1);
            draw_text("", 0, 0, 255, 255, 255, 255);
            if(newhud_itemspos_reverse_x) {
                adj = newhud_itemspos_centerfirst ? -th/2 : -th;
            } else {
                adj = newhud_itemspos_centerfirst ? -th/2 : 0;
            }
            drawicon(HICON_QUAD, xoff/itemsscale + adj, yoff/itemsscale - th/2.0, th);
            glPopMatrix();
        }
    }

    VARP(lagometer, 0, 0, 1);
    XIDENTHOOK(lagometer, IDF_EXTENDED);
    VARP(lagometernobg, 0, 0, 1);
    XIDENTHOOK(lagometernobg, IDF_EXTENDED);
    VARP(lagometerdisablewithgui, 0, 1, 1);
    XIDENTHOOK(lagometerdisablewithgui, IDF_EXTENDED);
    VARP(lagometerdisablelocal, 0, 1, 1);
    XIDENTHOOK(lagometerdisablelocal, IDF_EXTENDED);

    VARP(lagometershowping, 0, 1, 1);
    XIDENTHOOK(lagometershowping, IDF_EXTENDED);
    VARP(lagometeronlypingself, 0, 1, 1);
    XIDENTHOOK(lagometeronlypingself, IDF_EXTENDED);
    VARP(lagometerpingsz, 100, 150, 200);
    XIDENTHOOK(lagometerpingsz, IDF_EXTENDED);

    VARP(lagometerpos_x, 0, 10, 1000);
    XIDENTHOOK(lagometerpos_x, IDF_EXTENDED);
    VARP(lagometerpos_start_x, -1, 1, 1);
    XIDENTHOOK(lagometerpos_start_x, IDF_EXTENDED);
    VARP(lagometerpos_y, 0, 500, 1000);
    XIDENTHOOK(lagometerpos_y, IDF_EXTENDED);

    VARP(lagometerlen, 100, 100, LAGMETERDATASIZE);
    XIDENTHOOK(lagometerlen, IDF_EXTENDED);
    VARP(lagometerheight, 50, 100, 300);
    XIDENTHOOK(lagometerheight, IDF_EXTENDED);
    VARP(lagometercolsz, 1, 1, 3);
    XIDENTHOOK(lagometercolsz, IDF_EXTENDED);

    void drawlagmeter(int w, int h) {
        fpsent* d = (player1->state == CS_SPECTATOR) ? followingplayer() : player1;
        if(!d || (lagometerdisablewithgui && framehasgui) ||
           (lagometerdisablelocal && !connectedpeer() && !demoplayback)) return;

        int conw = int(w/staticscale), conh = int(h/staticscale);

        float xoff = lagometerpos_start_x == 1 ? (1000-lagometerpos_x)*conw/1000 : lagometerpos_x*conw/1000;
        float yoff = lagometerpos_y*conh/1000;
        float xpsz = lagometerlen/staticscale;
        float ypsz = lagometerheight/staticscale;

        glPushMatrix();
        glScalef(staticscale, staticscale, 1);
        if(lagometerpos_start_x == 1) {
            xoff -= lagometerlen*lagometercolsz/staticscale;
        } else if(lagometerpos_start_x == 0) {
            xoff -= lagometerlen*lagometercolsz/staticscale/2.0;
        }
        yoff -= ypsz/2.0;
        if( !(d == player1 && lagometeronlypingself)) {
            if(!lagometernobg) {
                drawacoloredquad(xoff,
                                 yoff,
                                 xpsz*lagometercolsz,
                                 ypsz,
                                 (GLubyte)255,
                                 (GLubyte)255,
                                 (GLubyte)255,
                                 (GLubyte)50);
            }
            float ch = 0.0;
            int len = min(d->lagdata.pj.len, lagometerlen);
            int pj;
            loopi(len) {
                ch = min(100, d->lagdata.pj[i])/staticscale * lagometerheight/100.0;
                pj = d->lagdata.pj[i];
                drawacoloredquad(xoff + (i*lagometercolsz)/staticscale,
                                 yoff + ypsz - ch,
                                 lagometercolsz/staticscale,
                                 ch,
                                 pj >= 100 ? (GLubyte)255 : (GLubyte)0,
                                 (GLubyte)0,
                                 pj >= 100 ? (GLubyte)0 : (GLubyte)255,
                                 (GLubyte)127);
            }
        }
        #define LAGOMETERBUFSZ 10
        if(lagometershowping) {
            char buff[LAGOMETERBUFSZ];
            glPushMatrix();
            float scale = lagometerpingsz/100.0;
            glScalef(scale, scale, 1);
            int w1=0, h1=0;
            int pl = getpacketloss();
            bool coloredpl = false;
            if(pl && d == player1) {
                snprintf(buff, LAGOMETERBUFSZ, "%d %d", d->ping, pl);
                coloredpl = true;
            } else {
                snprintf(buff, LAGOMETERBUFSZ, "%d", d->ping);
            }
            text_bounds(buff, w1, h1);
            if(d == player1 && lagometeronlypingself) {
                int gap = 0;
                if(lagometerpos_start_x == 1) {
                    gap = xpsz*lagometercolsz/scale - w1;
                } else if(lagometerpos_start_x == 0) {
                    gap = xpsz*lagometercolsz/scale - w1/2.0;
                }
                draw_text(buff, xoff/scale + gap,
                          (yoff + ypsz/2.0)/scale - h1/2,
                          255, coloredpl ? 0 : 255, coloredpl ? 0 : 255, 255);
            } else {
                draw_text(buff, (xoff + xpsz*lagometercolsz)/scale - w1 - h1/4 ,
                          yoff/scale,
                          255, coloredpl ? 0 : 255, coloredpl ? 0 : 255, 255);
            }
            glPopMatrix();
        }
        glPopMatrix();
    }

    ICOMMAND(extendedsettings, "", (), executestr("showgui extended_settings"));

    void gameplayhud(int w, int h) {
        if(player1->state==CS_SPECTATOR &&
           !(newhud && newhud_spectatorsdisablewithgui && framehasgui)) {
            drawspectator(w, h);
        }

        fpsent *d = hudplayer();
        if(d->state!=CS_EDITING) {
            if(newhud) {
                if(d->state!=CS_SPECTATOR) {
                    drawnewhudhp(d, w, h);
                    drawnewhudammo(d, w, h);
                    drawnewhuditems(d, w, h);
                }
                if(cmode) cmode->drawhud(d, w, h);
            } else {
                if(d->state!=CS_SPECTATOR) drawhudicons(d, w, h);
                if(cmode) cmode->drawhud(d, w, h);
            }
        }

        if(ammobar && d->state!=CS_EDITING && d->state!=CS_DEAD && d->state!=CS_SPECTATOR &&
           ! (ammobardisablewithgui && framehasgui) &&
           ! (m_insta && ammobardisableininsta)) {
            drawammobar(d, w, h);
        }

        if(gameclock && d->state!=CS_EDITING && !(gameclockdisablewithgui && framehasgui)) {
            drawclock(w, h);
        }

        if(hudscores && d->state!=CS_EDITING && !(hudscoresdisablewithgui && framehasgui)) {
            drawscores(w, h);
        }

        if(lagometer) {
            drawlagmeter(w, h);
        }
    }

    int clipconsole(int w, int h)
    {
        if(cmode) return cmode->clipconsole(w, h);
        return 0;
    }

    VARP(teamcrosshair, 0, 1, 1);
    VARP(hitcrosshair, 0, 425, 1000);

    const char *defaultcrosshair(int index)
    {
        switch(index)
        {
            case 2: return "data/hit.png";
            case 1: return "data/teammate.png";
            default: return "data/crosshair.png";
        }
    }

    int selectcrosshair(float &r, float &g, float &b)
    {
        fpsent *d = hudplayer();
        if(d->state==CS_SPECTATOR || d->state==CS_DEAD) return -1;

        if(d->state!=CS_ALIVE) return 0;

        int crosshair = 0;
        if(lasthit && lastmillis - lasthit < hitcrosshair) crosshair = 2;
        else if(teamcrosshair)
        {
            dynent *o = intersectclosest(d->o, worldpos, d);
            if(o && o->type==ENT_PLAYER && isteam(((fpsent *)o)->team, d->team))
            {
                crosshair = 1;
                r = g = 0;
            }
        }

        if(crosshair!=1 && !editmode && !m_insta)
        {
            if(d->health<=25) { r = 1.0f; g = b = 0; }
            else if(d->health<=50) { r = 1.0f; g = 0.5f; b = 0; }
        }
        if(d->gunwait) { r *= 0.5f; g *= 0.5f; b *= 0.5f; }
        return crosshair;
    }

    void lighteffects(dynent *e, vec &color, vec &dir)
    {
#if 0
        fpsent *d = (fpsent *)e;
        if(d->state!=CS_DEAD && d->quadmillis)
        {
            float t = 0.5f + 0.5f*sinf(2*M_PI*lastmillis/1000.0f);
            color.y = color.y*(1-t) + t;
        }
#endif
    }

    bool serverinfostartcolumn(g3d_gui *g, int i)
    {
        static const char * const names[] = { "ping ", "players ", "mode ", "map ", "time ", "master ", "host ", "port ", "description " };
        static const float struts[] =       { 7,       7,          12.5f,   14,      7,      8,         14,      7,       24.5f };
        if(size_t(i) >= sizeof(names)/sizeof(names[0])) return false;
        g->pushlist();
        g->text(names[i], 0xFFFF80, !i ? " " : NULL);
        if(struts[i]) g->strut(struts[i]);
        g->mergehits(true);
        return true;
    }

    void serverinfoendcolumn(g3d_gui *g, int i)
    {
        g->mergehits(false);
        g->column(i);
        g->poplist();
    }

    const char *mastermodecolor(int n, const char *unknown)
    {
        return (n>=MM_START && size_t(n-MM_START)<sizeof(mastermodecolors)/sizeof(mastermodecolors[0])) ? mastermodecolors[n-MM_START] : unknown;
    }

    const char *mastermodeicon(int n, const char *unknown)
    {
        return (n>=MM_START && size_t(n-MM_START)<sizeof(mastermodeicons)/sizeof(mastermodeicons[0])) ? mastermodeicons[n-MM_START] : unknown;
    }

    bool serverinfoentry(g3d_gui *g, int i, const char *name, int port, const char *sdesc, const char *map, int ping, const vector<int> &attr, int np)
    {
        if(ping < 0)
        {
            switch(i)
            {
                case 0:
                    if(g->button(" ", 0xFFFFDD, "serverunk")&G3D_UP) return true;
                    break;

                case 1:
                case 2:
                case 3:
                case 4:
                case 5:
                    if(g->button(" ", 0xFFFFDD)&G3D_UP) return true;
                    break;

                case 6:
                    if(g->buttonf("%s ", 0xFFFFDD, NULL, name)&G3D_UP) return true;
                    break;

                case 7:
                    if(g->buttonf("%d ", 0xFFFFDD, NULL, port)&G3D_UP) return true;
                    break;

                case 8:
                    if(g->button(sdesc, 0xFFFFDD)&G3D_UP) return true;
                    break;
            }
            return false;
        }

        if(attr.empty() || attr[0]!=PROTOCOL_VERSION)
        {
            switch(i)
            {
                case 0:
                    if(g->buttonf("%d ", 0xFFFFDD, "serverunk", ping)&G3D_UP) return true;
                    break;

                case 1:
                    if(attr.length()>=4)
                    {
                        if(g->buttonf(np >= attr[3] ? "\f3%d/%d " : "%d/%d ", 0xFFFFDD, NULL, np, attr[3])&G3D_UP) return true;
                    }
                    else if(g->buttonf("%d ", 0xFFFFDD, NULL, np)&G3D_UP) return true;
                    break;

                case 2:
                    if(g->buttonf("%s ", 0xFFFFDD, NULL, attr.length()>=2 ? server::modename(attr[1], "") : "")&G3D_UP) return true;
                    break;

                case 3:
                    if(g->buttonf("%.25s ", 0xFFFFDD, NULL, map)&G3D_UP) return true;
                    break;

                case 4:
                    if(attr.length()>=3 && attr[2] > 0)
                    {
                        int secs = clamp(attr[2], 0, 59*60+59),
                            mins = secs/60;
                        secs %= 60;
                        if(g->buttonf("%d:%02d ", 0xFFFFDD, NULL, mins, secs)&G3D_UP) return true;
                    }
                    else if(g->buttonf(" ", 0xFFFFDD)&G3D_UP) return true;
                    break;

                case 5:
                    if(g->buttonf("%s%s ", 0xFFFFDD, NULL, attr.length()>=5 ? mastermodecolor(attr[4], "") : "", attr.length()>=5 ? server::mastermodename(attr[4], "") : "")&G3D_UP) return true;
                    break;

                case 6:
                    if(g->buttonf("%s ", 0xFFFFDD, NULL, name)&G3D_UP) return true;
                    break;

                case 7:
                    if(g->buttonf("%d ", 0xFFFFDD, NULL, port)&G3D_UP) return true;
                    break;

                case 8:
                    if(g->buttonf("[%s protocol] ", 0xFFFFDD, NULL, attr.empty() ? "unknown" : (attr[0] < PROTOCOL_VERSION ? "older" : "newer"))&G3D_UP) return true;
                    break;
            }
            return false;
        }

        switch(i)
        {
            case 0:
            {
                const char *icon = attr.inrange(3) && np >= attr[3] ? "serverfull" : (attr.inrange(4) ? mastermodeicon(attr[4], "serverunk") : "serverunk");
                if(g->buttonf("%d ", 0xFFFFDD, icon, ping)&G3D_UP) return true;
                break;
            }

            case 1:
                if(attr.length()>=4)
                {
                    if(g->buttonf(np >= attr[3] ? "\f3%d/%d " : "%d/%d ", 0xFFFFDD, NULL, np, attr[3])&G3D_UP) return true;
                }
                else if(g->buttonf("%d ", 0xFFFFDD, NULL, np)&G3D_UP) return true;
                break;

            case 2:
                if(g->buttonf("%s ", 0xFFFFDD, NULL, attr.length()>=2 ? server::modename(attr[1], "") : "")&G3D_UP) return true;
                break;

            case 3:
                if(g->buttonf("%.25s ", 0xFFFFDD, NULL, map)&G3D_UP) return true;
                break;

            case 4:
                if(attr.length()>=3 && attr[2] > 0)
                {
                    int secs = clamp(attr[2], 0, 59*60+59),
                        mins = secs/60;
                    secs %= 60;
                    if(g->buttonf("%d:%02d ", 0xFFFFDD, NULL, mins, secs)&G3D_UP) return true;
                }
                else if(g->buttonf(" ", 0xFFFFDD)&G3D_UP) return true;
                break;
            case 5:
                if(g->buttonf("%s%s ", 0xFFFFDD, NULL, attr.length()>=5 ? mastermodecolor(attr[4], "") : "", attr.length()>=5 ? server::mastermodename(attr[4], "") : "")&G3D_UP) return true;
                break;

            case 6:
                if(g->buttonf("%s ", 0xFFFFDD, NULL, name)&G3D_UP) return true;
                break;

            case 7:
                if(g->buttonf("%d ", 0xFFFFDD, NULL, port)&G3D_UP) return true;
                break;

            case 8:
                if(g->buttonf("%.25s", 0xFFFFDD, NULL, sdesc)&G3D_UP) return true;
                break;
        }
        return false;
    }

    struct demolistentry {
        char *name;
        time_t mtime;

        demolistentry(char *nname, time_t nmtime) {
            size_t len = 0;
            if(nname) len = strlen(nname);
            if(len <= 0) {
                name = NULL;
            } else {
                name = new char[len+1];
                strcpy(name, nname);
            }
            mtime = nmtime;
        }

        ~demolistentry() {
            delete[] name;
        }
    };

    int demosortfn(struct demolistentry* &e1, struct demolistentry* &e2) {
        if(e1->mtime > e2->mtime) return 1;
        if(e1->mtime < e2->mtime) return 0;
        if(strcmp(e1->name, e2->name) <= 0) return 1;
        return 0;
    }

    static vector<demolistentry *> demos;

    static void listdirfiles(const char *dir, const char *ext, vector<char *> &files) {
        string dirname;
        copystring(dirname, dir);
        path(dirname);
        size_t dirlen = strlen(dirname);
        while(dirlen > 1 && dirname[dirlen-1] == PATHDIV) dirname[--dirlen] = '\0';
        string s;
        if(homedir[0]) {
            formatstring(s)("%s%s", homedir, dirname);
            listdir(homedir, false, ext, files);
        }
    }

    void updatedemoslist() {
        vector<char *> files;
        demos.deletecontents();
        listdirfiles("", "dmo", files);
        struct stat statbuf;
        struct demolistentry *pe;
        string s;
        loopv(files) {
            formatstring(s)("%s%s.dmo", homedir, files[i]);
            if(stat(s, &statbuf) == -1) continue;
            pe = new demolistentry(files[i], statbuf.st_mtime);
            demos.add(pe);
        }
        demos.sort(demosortfn);
    }

    #define FILTERLEN 51
    static char filter[FILTERLEN];

    static void applyfilter(vector<demolistentry *> &p0, vector<demolistentry *> &p1) {
        vector<bool> ismatched;
        loopv(p0) {
            ismatched.add(false);
        }
        const char* spl = " ";
        char* pats = strduplicate(filter);
        char* p = strtok(pats, spl);
        while(p) {
            loopv(p0) {
                if( p0[i] && strccasestr(p0[i]->name, p) != NULL && !ismatched[i]) {
                    p1.add( p0[i] );
                    ismatched[i] = true;
                }
            }
            p = strtok(NULL, spl);
        }
        free(pats);
    }


    static void demoheader(g3d_gui *g, int count, int maxcount) {
        g->pushlist();
        if(g->button("clear ", 0xFFFFDD, "action")&G3D_UP) {
            filter[0] = 0;
        }
        const char* f = g->field("", 0xFFFFFF, FILTERLEN-1, 0, filter);
        if(f) {
            strncpy(filter, f, FILTERLEN-1);
            filter[FILTERLEN-1] = 0;
        }

        g->spring();

        if(count == maxcount) {
            g->textf("%d", 0xFFFFDD, "info", count);

        } else {
            g->textf("%d/%d", 0xFFFFDD, "info", count, maxcount);
        }
        g->poplist();
        g->separator();
    }

    extern void setmode(int mode);
    extern void demoseekintermission();
    void showdemoslist(g3d_gui *g) {
        int fulllen, len;
        fulllen = demos.length();
        vector<demolistentry *> fdemos;
        if(strlen(filter) > 0) {
            applyfilter(demos, fdemos);
        } else {
            fdemos = demos;
        }
        len = fdemos.length();
        g->allowautotab(false);
        int maxcount = max(16, guiautotab-2);
        int count = 0;
        demoheader(g, len, fulllen);
        loopv(fdemos) {
            if(!fdemos[i]) continue;
            if(count >= maxcount) {
                g->tab();
                demoheader(g, len, fulllen);
                count = 0;
            }
            g->pushlist();
            if(g->buttonf("play  ", 0xFFFFDD, "action")&G3D_UP) {
                server::stopdemo();
                setmode(-1);
                changemap(fdemos[i]->name);
                g->poplist();
                g->allowautotab(true);
                return;
            }
            if(g->buttonf("toend", 0xFFFFDD, "action")&G3D_UP) {
                server::stopdemo();
                setmode(-1);
                changemap(fdemos[i]->name);
                demoseekintermission();
                g->poplist();
                g->allowautotab(true);
                return;
            }
            g->separator();
            g->textf("%.100s", 0xFFFFDD, NULL, fdemos[i]->name);
            g->poplist();
            count++;
        }
        if(count < maxcount) {
            g->pushlist();
            g->pushlist();
            g->strut(115);
            loopi( maxcount - count) {
                g->buttonf(" ", 0xFFFFDD, NULL);
            }
            g->poplist();
            g->poplist();
        }
        g->allowautotab(true);
    }

    COMMAND(updatedemoslist, "");

    // any data written into this vector will get saved with the map data. Must take care to do own versioning, and endianess if applicable. Will not get called when loading maps from other games, so provide defaults.
    void writegamedata(vector<char> &extras) {}
    void readgamedata(vector<char> &extras) {}

    const char *savedconfig() { return "config.cfg"; }
    const char *restoreconfig() { return "restore.cfg"; }
    const char *defaultconfig() { return "data/defaults.cfg"; }
    const char *autoexec() { return "autoexec.cfg"; }
    const char *savedservers() { return "servers.cfg"; }
    const char *ignoredservers() { return "ignoredservers.cfg"; }

    const char **getgamescripts() { return game_scripts; }

    void loadconfigs()
    {
        execfile("auth.cfg", false);
    }
}

