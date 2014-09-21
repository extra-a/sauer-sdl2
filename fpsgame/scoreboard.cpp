// creation of scoreboard
#include "game.h"

namespace game
{
    extern const char* getcurrentteam();

    fpsent* getcurrentplayer() {
        if(player1->state==CS_SPECTATOR && followingplayer()) {
            return followingplayer();
        }
        return player1;
    }

    int getgundamagetotal(int gun, fpsent* f = NULL) {
        fpsent* d = f ? f : getcurrentplayer();
        if(gun < 0) {
            int dmg = 0;
            loopi(MAXWEAPONS) {
                dmg += d->detaileddamagetotal[i];
            }
            return dmg;
        } else if(gun < MAXWEAPONS) {
            return d->detaileddamagetotal[gun];
        }
        return 0;
    }

    int getgundamagedealt(int gun, fpsent* f = NULL) {
        fpsent* d = f ? f : getcurrentplayer();
        if(gun < 0) {
            int dmg = 0;
            loopi(MAXWEAPONS) {
                dmg += d->detaileddamagedealt[i];
            }
            return dmg;
        } else if(gun < MAXWEAPONS) {
            return d->detaileddamagedealt[gun];
        }
        return 0;
    }

    int getgundamagereceived(int gun, fpsent* f = NULL) {
        fpsent* d = f ? f : getcurrentplayer();
        if(gun < 0) {
            int dmg = 0;
            loopi(MAXWEAPONS) {
                dmg += d->detaileddamagereceived[i];
            }
            return dmg;
        } else if(gun < MAXWEAPONS) {
            return d->detaileddamagereceived[gun];
        }
        return 0;
    }

    int getgundamagewasted(int gun, fpsent* f = NULL) {
        return getgundamagetotal(gun, f) - getgundamagedealt(gun, f);
    }

    int getgunnetdamage(int gun, fpsent* f = NULL) {
        return getgundamagedealt(gun, f) - getgundamagereceived(gun, f);
    }

    double getweaponaccuracy(int gun, fpsent* f = NULL) {
        double total = max(1.0, (double)getgundamagetotal(gun, f));
        return (getgundamagedealt(gun, f) / total) * 100;
    }
    

    VARP(scoreboard2d, 0, 1, 1);
    VARP(showservinfo, 0, 1, 1);
    VARP(showclientnum, 0, 0, 1);
    VARP(showpj, 0, 0, 1);
    VARP(showping, 0, 1, 1);
    VARP(showspectators, 0, 1, 1);
    VARP(highlightscore, 0, 1, 1);
    VARP(showconnecting, 0, 0, 1);

    VARP(showfrags, 0, 0, 1);
    XIDENTHOOK(showfrags, IDF_EXTENDED);
    VARP(showflags, 0, 0, 1);
    XIDENTHOOK(showflags, IDF_EXTENDED);

    VARP(shownetfrags, 0, 0, 1);
    XIDENTHOOK(shownetfrags, IDF_EXTENDED);
    VARP(netfragscolors, 0, 0, 1);
    XIDENTHOOK(netfragscolors, IDF_EXTENDED);

    VARP(showdamagedealt, 0, 0, 1);
    XIDENTHOOK(showdamagedealt, IDF_EXTENDED);
    VARP(shownetdamage, 0, 0, 1);
    XIDENTHOOK(shownetdamage, IDF_EXTENDED);
    VARP(netdamagecolors, 0, 0, 1);
    XIDENTHOOK(netdamagecolors, IDF_EXTENDED);

    VARP(showacc, 0, 0, 1);
    XIDENTHOOK(showacc, IDF_EXTENDED);

    static hashset<teaminfo> teaminfos;

    void clearteaminfo()
    {
        teaminfos.clear();
    }

    void setteaminfo(const char *team, int frags)
    {
        teaminfo *t = teaminfos.access(team);
        if(!t) { t = &teaminfos[team]; copystring(t->team, team, sizeof(t->team)); }
        t->frags = frags;
    }
            
    static inline bool playersort(const fpsent *a, const fpsent *b)
    {
        if(a->state==CS_SPECTATOR)
        {
            if(b->state==CS_SPECTATOR) return strcmp(a->name, b->name) < 0;
            else return false;
        }
        else if(b->state==CS_SPECTATOR) return true;
        if(m_ctf || m_collect)
        {
            if(a->flags > b->flags) return true;
            if(a->flags < b->flags) return false;
        }
        if(a->frags > b->frags) return true;
        if(a->frags < b->frags) return false;
        return strcmp(a->name, b->name) < 0;
    }

    void getbestplayers(vector<fpsent *> &best, bool fulllist)
    {
        loopv(players)
        {
            fpsent *o = players[i];
            if(o->state!=CS_SPECTATOR) best.add(o);
        }
        best.sort(playersort);
        if(!fulllist)
            while(best.length() > 1 && best.last()->frags < best[0]->frags)
                best.drop();
    }

    void getbestteams(vector<const char *> &best)
    {
        if(cmode && cmode->hidefrags()) 
        {
            vector<teamscore> teamscores;
            cmode->getteamscores(teamscores);
            teamscores.sort(teamscore::compare);
            while(teamscores.length() > 1 && teamscores.last().score < teamscores[0].score) teamscores.drop();
            loopv(teamscores) best.add(teamscores[i].team);
        }
        else 
        {
            int bestfrags = INT_MIN;
            enumerates(teaminfos, teaminfo, t, bestfrags = max(bestfrags, t.frags));
            if(bestfrags <= 0) loopv(players)
            {
                fpsent *o = players[i];
                if(o->state!=CS_SPECTATOR && !teaminfos.access(o->team) && best.htfind(o->team) < 0) { bestfrags = 0; best.add(o->team); } 
            }
            enumerates(teaminfos, teaminfo, t, if(t.frags >= bestfrags) best.add(t.team));
        }
    }

    static vector<scoregroup *> groups;
    static vector<fpsent *> spectators;

    vector<scoregroup *> getscoregroups() {
        return groups;
    }

    static inline bool scoregroupcmp(const scoregroup *x, const scoregroup *y)
    {
        if(!x->team)
        {
            if(y->team) return false;
        }
        else if(!y->team) return true;
        if(x->score > y->score) return true;
        if(x->score < y->score) return false;
        if(x->players.length() > y->players.length()) return true;
        if(x->players.length() < y->players.length()) return false;
        return x->team && y->team && strcmp(x->team, y->team) < 0;
    }

    int groupplayers()
    {
        int numgroups = 0;
        spectators.setsize(0);
        loopv(players)
        {
            fpsent *o = players[i];
            if(!showconnecting && !o->name[0]) continue;
            if(o->state==CS_SPECTATOR) { spectators.add(o); continue; }
            const char *team = m_teammode && o->team[0] ? o->team : NULL;
            bool found = false;
            loopj(numgroups)
            {
                scoregroup &g = *groups[j];
                if(team!=g.team && (!team || !g.team || strcmp(team, g.team))) continue;
                g.players.add(o);
                found = true;
            }
            if(found) continue;
            if(numgroups>=groups.length()) groups.add(new scoregroup);
            scoregroup &g = *groups[numgroups++];
            g.team = team;
            if(!team) g.score = 0;
            else if(cmode && cmode->hidefrags()) g.score = cmode->getteamscore(o->team);
            else { teaminfo *ti = teaminfos.access(team); g.score = ti ? ti->frags : 0; }
            g.players.setsize(0);
            g.players.add(o);
        }
        loopi(numgroups) groups[i]->players.sort(playersort);
        spectators.sort(playersort);
        groups.sort(scoregroupcmp, 0, numgroups);
        return numgroups;
    }

    void formatdmg(char* buff, int bufflen, int d) {
        if(abs(d) < 1000) {
            snprintf(buff, bufflen, "%d", d);
        } else {
            snprintf(buff, bufflen, "%d.%dk", d/1000, abs((d%1000)/100));
        }
    }

    void getcolor(int val, int &color) {
        if(val>=0) {
            color = 0x00FF00;
        } else {
            color = 0xFF0000;
        }
    }

    void fragwrapper(g3d_gui &g, int frags, int deaths) {
        g.pushlist();
        g.textf("%d", 0xFFFFDD, NULL, frags);
        int net = frags-deaths, c = 0xFFFFDD;
        if(netfragscolors) getcolor(net, c);
        g.textf(":", 0x888888, NULL);
        g.textf("%d", c, NULL, net);
        g.poplist();
    }

    void dmgwrapper(g3d_gui &g, int dealt, int net) {
        char buff[10];
        g.pushlist();
        formatdmg(buff, 10, dealt);
        g.textf("%s", 0xFFFFDD, NULL, buff);
        if(shownetdamage) {
            int c = 0xFFFFDD;
            if(netdamagecolors) getcolor(net, c);
            g.textf(":", 0x888888, NULL);
            formatdmg(buff, 10, net);
            g.textf("%s", c, NULL, buff);
        }
        g.poplist();
    }

    void renderscoreboard(g3d_gui &g, bool firstpass)
    {
        const ENetAddress *address = connectedpeer();
        if(showservinfo && address)
        {
            string hostname;
            if(enet_address_get_host_ip(address, hostname, sizeof(hostname)) >= 0)
            {
                if(servinfo[0]) g.titlef("%.25s", 0xFFFF80, NULL, servinfo);
                else g.titlef("%s:%d", 0xFFFF80, NULL, hostname, address->port);
            }
        }
     
        g.pushlist();
        g.spring();
        g.text(server::modename(gamemode), 0xFFFF80);
        g.separator();
        const char *mname = getclientmap();
        g.text(mname[0] ? mname : "[new map]", 0xFFFF80);
        extern int gamespeed;
        if(gamespeed != 100) { g.separator(); g.textf("%d.%02dx", 0xFFFF80, NULL, gamespeed/100, gamespeed%100); }
        if(m_timed && mname[0] && (maplimit >= 0 || intermission))
        {
            g.separator();
            if(intermission) g.text("intermission", 0xFFFF80);
            else 
            {
                int secs = max(maplimit-lastmillis, 0)/1000, mins = secs/60;
                secs %= 60;
                g.pushlist();
                g.strut(mins >= 10 ? 4.5f : 3.5f);
                g.textf("%d:%02d", 0xFFFF80, NULL, mins, secs);
                g.poplist();
            }
        }
        if(ispaused()) { g.separator(); g.text("paused", 0xFFFF80); }
        g.spring();
        g.poplist();

        g.separator();
 
        int numgroups = groupplayers();
        loopk(numgroups)
        {
            if((k%2)==0) g.pushlist(); // horizontal
            
            scoregroup &sg = *groups[k];
            int bgcolor = sg.team && m_teammode ? (isteam(getcurrentteam(), sg.team) ? 0x3030C0 : 0xC03030) : 0,
                fgcolor = 0xFFFF80;

            g.pushlist(); // vertical
            g.pushlist(); // horizontal

            #define loopscoregroup(o, b) \
                loopv(sg.players) \
                { \
                    fpsent *o = sg.players[i]; \
                    b; \
                }    

            g.pushlist();
            if(sg.team && m_teammode)
            {
                g.pushlist();
                g.background(bgcolor, numgroups>1 ? 3 : 5);
                g.strut(1);
                g.poplist();
            }
            g.text("", 0, " ");
            loopscoregroup(o,
            {
                if(o==player1 && highlightscore && (multiplayer(false) || demoplayback || players.length() > 1))
                {
                    g.pushlist();
                    g.background(0x808080, numgroups>1 ? 3 : 5);
                }
                const playermodelinfo &mdl = getplayermodelinfo(o);
                const char *icon = sg.team && m_teammode ? (isteam(getcurrentteam(), sg.team) ? mdl.blueicon : mdl.redicon) : mdl.ffaicon;
                g.text("", 0, icon);
                if(o==player1 && highlightscore && (multiplayer(false) || demoplayback || players.length() > 1)) g.poplist();
            });
            g.poplist();

            if(sg.team && m_teammode)
            {
                g.pushlist(); // vertical

                if(sg.score>=10000) g.textf("%s: WIN", fgcolor, NULL, sg.team);
                else g.textf("%s: %d", fgcolor, NULL, sg.team, sg.score);

                g.pushlist(); // horizontal
            }

            if((m_ctf || m_collect) && showflags)
            {
               g.pushlist();
               g.strut(m_ctf?5:6);
               g.text(m_ctf?"flags":"skulls", fgcolor);
               loopscoregroup(o, g.textf("%d", 0xFFFFDD, NULL, o->flags));
               g.poplist();
            }

            if(!cmode || !cmode->hidefrags() || showfrags)
            { 
                g.pushlist();
                g.strut(shownetfrags ? 7 : 5);
                g.text("frags", fgcolor);
                if(shownetfrags) {
                    loopscoregroup(o, fragwrapper(g, o->frags, o->deaths));
                } else {
                    loopscoregroup(o, g.textf("%d", 0xFFFFDD, NULL, o->frags));
                }
                g.poplist();
            }

            g.pushlist();
            g.text("name", fgcolor);
            g.strut(15);
            loopscoregroup(o, 
            {
                int status = o->state!=CS_DEAD ? 0xFFFFDD : 0x606060;
                if(o->privilege)
                {
                    status = o->privilege>=PRIV_ADMIN ? 0xFF8000 : 0x40FF80;
                    if(o->state==CS_DEAD) status = (status>>1)&0x7F7F7F;
                }
                g.textf("%s ", status, NULL, colorname(o));
            });
            g.poplist();

            if(showdamagedealt)
            {
                g.pushlist();
                g.strut(shownetdamage ? 10 : 6);
                g.text("dmg", fgcolor);
                loopscoregroup(o, dmgwrapper(g, getgundamagedealt(-1,o), getgunnetdamage(-1,o)));
                g.poplist();
            }

            if(showacc)
            {
                g.pushlist();
                g.strut(6);
                g.text("acc", fgcolor);
                loopscoregroup(o, g.textf("%.2lf", 0xFFFFDD, NULL, getweaponaccuracy(-1,o)));
                g.poplist();
            }

            if(multiplayer(false) || demoplayback)
            {
                if(showpj)
                {
                    g.pushlist();
                    g.strut(5);
                    g.text("pj", fgcolor);
                    loopscoregroup(o,
                    {
                        if(o->state==CS_LAGGED) g.text("LAG", 0xFFFFDD);
                        else g.textf("%d", 0xFFFFDD, NULL, o->plag);
                    });
                    g.poplist();
                }

                if(showping)
                {
                    g.pushlist();
                    g.text("ping", fgcolor);
                    g.strut(5);
                    loopscoregroup(o,
                    {
                        fpsent *p = o->ownernum >= 0 ? getclient(o->ownernum) : o;
                        if(!p) p = o;
                        if(!showpj && p->state==CS_LAGGED) g.text("LAG", 0xFFFFDD);
                        else g.textf("%d", 0xFFFFDD, NULL, p->ping);
                    });
                    g.poplist();
                }
            }

            if(showclientnum || player1->privilege>=PRIV_MASTER)
            {
                g.pushlist();
                g.text("cn", fgcolor);
                loopscoregroup(o, g.textf("%d", 0xFFFFDD, NULL, o->clientnum));
                g.poplist();
            }
            
            if(sg.team && m_teammode)
            {
                g.poplist(); // horizontal
                g.poplist(); // vertical
            }

            g.poplist(); // horizontal
            g.poplist(); // vertical

            if(k+1<numgroups && (k+1)%2) g.space(2);
            else g.poplist(); // horizontal
        }
        
        if(showspectators && spectators.length())
        {
            if(showclientnum || player1->privilege>=PRIV_MASTER)
            {
                g.pushlist();
                
                g.pushlist();
                g.text("spectator", 0xFFFF80, " ");
                loopv(spectators) 
                {
                    fpsent *o = spectators[i];
                    int status = 0xFFFFDD;
                    if(o->privilege) status = o->privilege>=PRIV_ADMIN ? 0xFF8000 : 0x40FF80;
                    if(o==player1 && highlightscore)
                    {
                        g.pushlist();
                        g.background(0x808080, 3);
                    }
                    g.text(colorname(o), status, "spectator");
                    if(o==player1 && highlightscore) g.poplist();
                }
                g.poplist();

                g.space(1);
                g.pushlist();
                g.text("cn", 0xFFFF80);
                loopv(spectators) g.textf("%d", 0xFFFFDD, NULL, spectators[i]->clientnum);
                g.poplist();

                if(showping){
                       g.space(1);
                       g.pushlist();
                       g.text("ping", 0xFFFF80);
                       loopv(spectators){
                               fpsent *p = spectators[i]->ownernum >= 0 ? getclient(spectators[i]->ownernum) : spectators[i];
                               if(!p) p = spectators[i];
                               if(p->state==CS_LAGGED) g.text("LAG", 0xFFFFDD);
                               else g.textf("%d", 0xFFFFDD, NULL, p->ping);
                       }
                       g.poplist();
                }

                g.poplist();
            }
            else
            {
                g.textf("%d spectator%s", 0xFFFF80, " ", spectators.length(), spectators.length()!=1 ? "s" : "");
                loopv(spectators)
                {
                    if((i%3)==0) 
                    {
                        g.pushlist();
                        g.text("", 0xFFFFDD, "spectator");
                    }
                    fpsent *o = spectators[i];
                    int status = 0xFFFFDD;
                    if(o->privilege) status = o->privilege>=PRIV_ADMIN ? 0xFF8000 : 0x40FF80;
                    if(o==player1 && highlightscore)
                    {
                        g.pushlist();
                        g.background(0x808080);
                    }
                    g.text(colorname(o), status);
                    if(o==player1 && highlightscore) g.poplist();
                    if(i+1<spectators.length() && (i+1)%3) g.space(1);
                    else g.poplist();
                }
            }
        }
    }

    struct scoreboardgui : g3d_callback
    {
        bool showing;
        vec menupos;
        int menustart;

        scoreboardgui() : showing(false) {}

        void show(bool on)
        {
            if(!showing && on)
            {
                menupos = menuinfrontofplayer();
                menustart = starttime();
            }
            showing = on;
        }

        void gui(g3d_gui &g, bool firstpass)
        {
            g.start(menustart, 0.03f, NULL, false);
            renderscoreboard(g, firstpass);
            g.end();
        }

        void render()
        {
            if(showing) g3d_addgui(this, menupos, (scoreboard2d ? GUI_FORCE_2D : GUI_2D | GUI_FOLLOW) | GUI_BOTTOM);
        }

    } scoreboard;


    static void rendericon(g3d_gui &g, int i) {
        int icons[MAXWEAPONS] = {0, GUN_SG, GUN_CG, GUN_RL, GUN_RIFLE, GUN_GL, GUN_PISTOL};
        g.textwithtextureicon(NULL, 0, "packages/hud/items.png", false, true,
                              0.25f*((HICON_FIST+icons[i])%4),
                              0.25f*((HICON_FIST+icons[i])/4),
                              0.25f, 0.25f);
    }

    void renderplayerstats(g3d_gui &g, bool firstpass) {
        g.titlef("Stats for %s(%d)", 0xFFFFFF, NULL,
                 getcurrentplayer()->name, getcurrentplayer()->clientnum);
        g.separator();

        g.pushlist();

        g.pushlist();
        g.space(1);
        g.strut(4);
        loopi(MAXWEAPONS) {
            rendericon(g, i);
        }
        g.space(1);
        g.poplist();

        g.space(2);

        g.pushlist();
        g.text("Acc", 0xFFFF80);
        g.strut(6);
        loopi(MAXWEAPONS) {
            g.textf("%.2lf", 0xFFFFFF, NULL, getweaponaccuracy(i));
        }
        g.space(1);
        g.textf("%.2lf", 0x00C8FF, NULL, getweaponaccuracy(-1));
        g.poplist();

        g.space(2);

        g.pushlist();
        g.text("Dmg", 0xFFFF80);
        g.strut(6);
        loopi(MAXWEAPONS) {
            g.textf("%d", 0xFFFFFF, NULL, getgundamagedealt(i));
        }
        g.space(1);
        g.textf("%d", 0xFFFFFF, NULL, getgundamagedealt(-1));
        g.poplist();

        g.space(2);

        g.pushlist();
        g.text("Taken", 0xFFFF80);
        g.strut(6);
        loopi(MAXWEAPONS) {
            g.textf("%d", 0xFFFFFF, NULL, getgundamagereceived(i));
        }
        g.space(1);
        g.textf("%d", 0xFFFFFF, NULL, getgundamagereceived(-1));
        g.poplist();

        g.space(2);

        g.pushlist();
        g.text("Net", 0xFFFF80);
        g.strut(6);
        int net = 0, color = 0;
        loopi(MAXWEAPONS) {
            net = getgunnetdamage(i);
            g.textf("%d", 0xFFFFFF, NULL, net);
        }
        net = getgunnetdamage(-1);
        getcolor(net, color);
        g.space(1);
        g.textf("%d", color, NULL, net);
        g.poplist();

        g.poplist();
    }

    struct playerstatsgui : g3d_callback {
        bool showing;
        vec menupos;
        int menustart;

        playerstatsgui() : showing(false) {}

        void show(bool on) {
            if(!showing && on)
            {
                menupos = menuinfrontofplayer();
                menustart = starttime();
            }
            showing = on;
        }

        void gui(g3d_gui &g, bool firstpass)  {
            g.start(menustart, 0.03f, NULL, false);
            renderplayerstats(g, firstpass);
            g.end();
        }

        void render()  {
            if(showing)
                g3d_addgui(this, menupos, (scoreboard2d ? GUI_FORCE_2D : GUI_2D | GUI_FOLLOW) | GUI_BOTTOM);
        }

    } playerstats;


    void g3d_gamemenus()
    {
        playerstats.render();
        scoreboard.render();
    }

    // scoreboard
    VARFN(scoreboard, showscoreboard, 0, 0, 1, scoreboard.show(showscoreboard!=0));
    void showscores(bool on)
    {
        showscoreboard = on ? 1 : 0;
        scoreboard.show(on);
    }
    ICOMMAND(showscores, "D", (int *down), showscores(*down!=0));

    // player stats
    void showplayerstats(bool on)
    {
        playerstats.show(on);
    }
    ICOMMAND(showplayerstats, "D", (int *down), showplayerstats(*down!=0));

}

