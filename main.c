// Minish Quest - an original Zelda-style adventure for Nintendo DS / DSi (libnds)
// D-Pad: move | A: sword | X: boomerang | Y: fire bolt (uses mana)
// B: mount / dismount horse | hold R: shrink to tiny size (pass through bushes)
// Goal: find the 3 crystals hidden in the far rooms. START: restart.
#include <nds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 256
#define H 192
#define MAXE 4
#define C(r, g, b) (RGB15(r, g, b) | BIT(15))

static u16 buf[W * H];
static u8 map[12][16];           // 0 grass, 1 tree (solid), 2 bush (solid unless tiny)
static int rx, ry;               // current room (3 x 2 world)
static u8 cleared[2][3], taken[2][3];

typedef struct { int x, y, hp, alive, inv, dx, dy; } Enemy;
static Enemy en[MAXE];

static int px, py, face, hdir, hp, mana, crystals, rupees, mounted, tiny;
static int pinv, swordT, state, frame;
static int bAct, bx, by, bdx, bdy, bT, bRet;   // boomerang
static int fAct, fx, fy, fdx, fdy;             // fire bolt

static const int ddx[4] = {0, 0, -1, 1};
static const int ddy[4] = {-1, 1, 0, 0};

static void rect(int x, int y, int w, int h, u16 c) {
    for (int j = y; j < y + h; j++) {
        if (j < 0 || j >= H) continue;
        for (int i = x; i < x + w; i++) {
            if (i < 0 || i >= W) continue;
            buf[j * W + i] = c;
        }
    }
}

static int hit(int ax, int ay, int aw, int ah, int bx_, int by_, int bw, int bh) {
    return ax < bx_ + bw && ax + aw > bx_ && ay < by_ + bh && ay + ah > by_;
}

static int sgn(int v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }
static int absi(int v) { return v < 0 ? -v : v; }

static int tileAt(int x, int y) {
    if (x < 0 || x >= W || y < 0 || y >= H) return 0;
    return map[y >> 4][x >> 4];
}

static int blocked(int x, int y, int w, int h, int tn) {
    int xs[2] = {x, x + w - 1};
    int ys[2] = {y, y + h - 1};
    for (int a = 0; a < 2; a++)
        for (int b = 0; b < 2; b++) {
            int t = tileAt(xs[a], ys[b]);
            if (t == 1 || (t == 2 && !tn)) return 1;
        }
    return 0;
}

static unsigned hsh(int a, int b, int c, int d) {
    unsigned h = (unsigned)a * 73856093u ^ (unsigned)b * 19349663u ^
                 (unsigned)c * 83492791u ^ (unsigned)d * 2654435761u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}

static int hasCrystal(int x, int y) {
    return (x == 2 && y == 0) || (x == 0 && y == 1) || (x == 2 && y == 1);
}

static int pw(void) { return tiny ? 6 : (mounted ? 20 : 12); }
static int ph(void) { return tiny ? 6 : (mounted ? 14 : 12); }

static void buildRoom(void) {
    for (int ty = 0; ty < 12; ty++)
        for (int tx = 0; tx < 16; tx++) {
            int t = 0;
            int border = (tx == 0 || tx == 15 || ty == 0 || ty == 11);
            int lane = (ty == 5 || ty == 6 || tx == 7 || tx == 8);
            if (border) {
                t = 1;
                if ((tx == 0 && ty >= 5 && ty <= 6 && rx > 0) ||
                    (tx == 15 && ty >= 5 && ty <= 6 && rx < 2) ||
                    (ty == 0 && tx >= 7 && tx <= 8 && ry > 0) ||
                    (ty == 11 && tx >= 7 && tx <= 8 && ry < 1))
                    t = 0;
            } else if (!lane) {
                unsigned h = hsh(rx, ry, tx, ty) % 100;
                if (h < 9) t = 1;
                else if (h < 16) t = 2;
            }
            map[ty][tx] = t;
        }
}

static void spawnEnemies(void) {
    memset(en, 0, sizeof(en));
    if (cleared[ry][rx]) return;
    for (int i = 0; i < 3; i++) {
        for (int tries = 0; tries < 60; tries++) {
            int tx = 2 + rand() % 12, ty = 2 + rand() % 8;
            if (map[ty][tx] != 0) continue;
            if (absi(tx * 16 - px) + absi(ty * 16 - py) < 70) continue;
            en[i].x = tx * 16 + 2; en[i].y = ty * 16 + 2;
            en[i].hp = 2; en[i].alive = 1;
            break;
        }
    }
}

static void enterRoom(void) {
    buildRoom();
    spawnEnemies();
    bAct = 0; fAct = 0;
}

static void resetGame(void) {
    rx = 0; ry = 0; px = 120; py = 90; face = 1; hdir = 3;
    hp = 6; mana = 100; crystals = 0; rupees = 0;
    mounted = 0; tiny = 0; pinv = 0; swordT = 0; state = 0;
    memset(cleared, 0, sizeof(cleared));
    memset(taken, 0, sizeof(taken));
    enterRoom();
}

static void moveStep(int dx, int dy) {
    if (!blocked(px + dx, py + dy, pw(), ph(), tiny)) { px += dx; py += dy; }
}

static void hurtEnemy(Enemy *e, int dmg) {
    if (e->inv > 0) return;
    e->hp -= dmg;
    e->inv = 15;
    int kx = sgn((e->x + 6) - (px + pw() / 2));
    int ky = sgn((e->y + 6) - (py + ph() / 2));
    for (int i = 0; i < 8; i++) {
        if (!blocked(e->x + kx, e->y, 12, 12, 0)) e->x += kx;
        if (!blocked(e->x, e->y + ky, 12, 12, 0)) e->y += ky;
    }
    if (e->hp <= 0) {
        e->alive = 0;
        rupees += 5;
        int left = 0;
        for (int i = 0; i < MAXE; i++) left += en[i].alive;
        if (!left) cleared[ry][rx] = 1;
    }
}

static void cutBush(int x, int y) {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    if (map[y >> 4][x >> 4] == 2) { map[y >> 4][x >> 4] = 0; rupees++; }
}

static void update(int held, int down) {
    // mode switching (tiny / mounted) only if there is room for the new size
    int nt = (held & KEY_R) ? 1 : 0;
    int nm = mounted;
    if (down & KEY_B) nm = !mounted;
    if (nt != tiny || nm != mounted) {
        int w = nt ? 6 : (nm ? 20 : 12), h = nt ? 6 : (nm ? 14 : 12);
        if (!blocked(px, py, w, h, nt)) { tiny = nt; mounted = nm; }
    }

    // movement
    int sp = tiny ? 1 : (mounted ? 3 : 2);
    int dx = 0, dy = 0;
    if (held & KEY_LEFT)  { dx = -1; face = 2; hdir = 2; }
    if (held & KEY_RIGHT) { dx = 1;  face = 3; hdir = 3; }
    if (held & KEY_UP)    { dy = -1; face = 0; }
    if (held & KEY_DOWN)  { dy = 1;  face = 1; }
    for (int i = 0; i < sp; i++) {
        if (dx) moveStep(dx, 0);
        if (dy) moveStep(0, dy);
    }

    // room transitions
    if (px < 0) {
        if (rx > 0) { rx--; px = W - pw() - 2; enterRoom(); } else px = 0;
    } else if (px + pw() > W) {
        if (rx < 2) { rx++; px = 2; enterRoom(); } else px = W - pw();
    }
    if (py < 0) {
        if (ry > 0) { ry--; py = H - ph() - 2; enterRoom(); } else py = 0;
    } else if (py + ph() > H) {
        if (ry < 1) { ry++; py = 2; enterRoom(); } else py = H - ph();
    }

    int cx = px + pw() / 2, cy = py + ph() / 2;

    // weapons
    if ((down & KEY_A) && swordT == 0) swordT = 10;
    if ((down & KEY_X) && !bAct) {
        bAct = 1; bx = cx; by = cy; bdx = ddx[face]; bdy = ddy[face]; bT = 0; bRet = 0;
    }
    if ((down & KEY_Y) && !fAct && mana >= 25) {
        fAct = 1; fx = cx; fy = cy; fdx = ddx[face]; fdy = ddy[face]; mana -= 25;
    }
    if (frame % 6 == 0 && mana < 100) mana++;

    // sword
    if (swordT > 0) {
        int sx, sy, sw, sh;
        int L = tiny ? 8 : (mounted ? 20 : 16);
        switch (face) {
            case 0: sx = cx - 6; sy = py - L;      sw = 12; sh = L; break;
            case 1: sx = cx - 6; sy = py + ph();   sw = 12; sh = L; break;
            case 2: sx = px - L; sy = cy - 6;      sw = L;  sh = 12; break;
            default: sx = px + pw(); sy = cy - 6;  sw = L;  sh = 12; break;
        }
        cutBush(sx + sw / 2, sy + sh / 2);
        for (int i = 0; i < MAXE; i++)
            if (en[i].alive && hit(sx, sy, sw, sh, en[i].x, en[i].y, 12, 12))
                hurtEnemy(&en[i], 1);
        swordT--;
    }

    // boomerang
    if (bAct) {
        if (!bRet) {
            bx += bdx * 4; by += bdy * 4; bT++;
            if (bT >= 18 || tileAt(bx, by) == 1) bRet = 1;
        } else {
            int vx = cx - bx, vy = cy - by;
            if (absi(vx) <= 5 && absi(vy) <= 5) bAct = 0;
            else {
                bx += sgn(vx) * (absi(vx) < 4 ? absi(vx) : 4);
                by += sgn(vy) * (absi(vy) < 4 ? absi(vy) : 4);
            }
        }
        if (bAct) {
            cutBush(bx, by);
            for (int i = 0; i < MAXE; i++)
                if (en[i].alive && hit(bx - 3, by - 3, 6, 6, en[i].x, en[i].y, 12, 12))
                    hurtEnemy(&en[i], 1);
        }
    }

    // fire bolt
    if (fAct) {
        fx += fdx * 5; fy += fdy * 5;
        if (fx < 0 || fx >= W || fy < 0 || fy >= H) fAct = 0;
        else {
            int t = tileAt(fx, fy);
            if (t == 1) fAct = 0;
            else if (t == 2) { cutBush(fx, fy); fAct = 0; }
        }
        for (int i = 0; fAct && i < MAXE; i++)
            if (en[i].alive && hit(fx - 3, fy - 3, 6, 6, en[i].x, en[i].y, 12, 12)) {
                hurtEnemy(&en[i], 2); fAct = 0;
            }
    }

    // enemies
    if (pinv) pinv--;
    for (int i = 0; i < MAXE; i++) {
        Enemy *e = &en[i];
        if (!e->alive) continue;
        if (e->inv) e->inv--;
        if (frame & 1) {
            int vx = cx - (e->x + 6), vy = cy - (e->y + 6);
            if (absi(vx) + absi(vy) < 100) { e->dx = sgn(vx); e->dy = sgn(vy); }
            else if (frame % 60 == 0 || (e->dx == 0 && e->dy == 0)) {
                e->dx = rand() % 3 - 1; e->dy = rand() % 3 - 1;
            }
            if (!blocked(e->x + e->dx, e->y, 12, 12, 0)) e->x += e->dx;
            if (!blocked(e->x, e->y + e->dy, 12, 12, 0)) e->y += e->dy;
        }
        if (!pinv && hit(px, py, pw(), ph(), e->x, e->y, 12, 12)) { hp--; pinv = 60; }
    }

    // crystal
    if (hasCrystal(rx, ry) && !taken[ry][rx] &&
        hit(px, py, pw(), ph(), 121, 86, 14, 20)) {
        taken[ry][rx] = 1; crystals++;
        if (crystals >= 3) state = 2;
    }
    if (hp <= 0) state = 1;
}

static void drawPlayer(void) {
    if (pinv & 4) return;
    u16 tunic = C(3, 20, 5), dark = C(2, 13, 3), skin = C(30, 24, 18);
    if (tiny) {
        rect(px, py, 6, 6, tunic);
        rect(px, py, 6, 2, dark);
    } else if (mounted) {
        u16 brown = C(18, 10, 4), leg = C(10, 5, 2);
        rect(px, py + 2, 20, 8, brown);
        if (hdir == 2) rect(px - 5, py, 6, 6, brown); else rect(px + 19, py, 6, 6, brown);
        rect(hdir == 2 ? px + 20 : px - 4, py + 3, 4, 3, leg);
        int s = (frame / 6) & 1;
        rect(px + 2, py + 10, 3, 4 - s, leg);
        rect(px + 14, py + 10, 3, 3 + s, leg);
        rect(px + 7, py - 6, 6, 8, tunic);
        rect(px + 7, py - 10, 6, 4, skin);
        rect(px + 6, py - 13, 8, 4, dark);
    } else {
        rect(px + 2, py + 5, 8, 7, tunic);
        rect(px + 3, py + 1, 6, 5, skin);
        rect(px + 2, py - 2, 8, 4, dark);
    }
}

static void draw(void) {
    for (int ty = 0; ty < 12; ty++)
        for (int tx = 0; tx < 16; tx++) {
            int x = tx * 16, y = ty * 16;
            rect(x, y, 16, 16, ((tx + ty) & 1) ? C(6, 18, 6) : C(7, 20, 7));
            if (map[ty][tx] == 1) {
                rect(x + 2, y + 1, 12, 11, C(2, 10, 3));
                rect(x + 6, y + 11, 4, 5, C(12, 7, 2));
            } else if (map[ty][tx] == 2) {
                rect(x + 1, y + 4, 14, 11, C(10, 24, 8));
                rect(x + 4, y + 7, 3, 3, C(4, 15, 4));
                rect(x + 10, y + 9, 3, 3, C(4, 15, 4));
            }
        }
    if (hasCrystal(rx, ry) && !taken[ry][rx]) {
        rect(125, 86, 6, 20, C(8, 28, 31));
        rect(121, 91, 14, 10, C(8, 28, 31));
        rect(126, 90, 4, 12, C(31, 31, 31));
    }
    for (int i = 0; i < MAXE; i++)
        if (en[i].alive) {
            Enemy *e = &en[i];
            rect(e->x, e->y, 12, 12, (e->inv & 4) ? C(31, 31, 31) : C(28, 3, 3));
            rect(e->x + 2, e->y + 3, 3, 3, C(31, 31, 31));
            rect(e->x + 7, e->y + 3, 3, 3, C(31, 31, 31));
        }
    if (bAct) rect(bx - 3, by - 3, 6, 6, (frame & 2) ? C(31, 28, 4) : C(26, 20, 2));
    if (fAct) { rect(fx - 3, fy - 3, 6, 6, C(31, 14, 2)); rect(fx - 1, fy - 1, 2, 2, C(31, 30, 8)); }
    drawPlayer();
    if (swordT > 0) {
        int cx = px + pw() / 2, cy = py + ph() / 2;
        int L = tiny ? 8 : (mounted ? 20 : 16);
        u16 sc = C(28, 29, 31);
        switch (face) {
            case 0: rect(cx - 1, py - L, 3, L, sc); break;
            case 1: rect(cx - 1, py + ph(), 3, L, sc); break;
            case 2: rect(px - L, cy - 1, L, 3, sc); break;
            default: rect(px + pw(), cy - 1, L, 3, sc); break;
        }
    }
}

static void hud(void) {
    char h[4];
    for (int i = 0; i < 3; i++) h[i] = hp >= 2 * i + 2 ? 'O' : (hp == 2 * i + 1 ? 'o' : '.');
    h[3] = 0;
    iprintf("\x1b[0;0H MINISH QUEST              ");
    iprintf("\x1b[2;1HHP: %s   Mana: %3d   ", h, mana);
    iprintf("\x1b[3;1HRupees: %-4d Crystals: %d/3 ", rupees, crystals);
    iprintf("\x1b[4;1HRoom: %d,%d  %s       ", rx, ry, tiny ? "TINY " : (mounted ? "HORSE" : "FOOT "));
    iprintf("\x1b[7;1HA: sword     X: boomerang");
    iprintf("\x1b[8;1HY: fire bolt B: horse");
    iprintf("\x1b[9;1HR (hold): tiny size");
    iprintf("\x1b[11;1HFind 3 crystals!");
    if (state == 1) iprintf("\x1b[14;1HGAME OVER - press START");
    else if (state == 2) iprintf("\x1b[14;1HYOU WIN!  - press START");
    else iprintf("\x1b[14;1H                       ");
}

int main(void) {
    videoSetMode(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    int bg = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    u16 *fb = bgGetGfxPtr(bg);
    consoleDemoInit();
    resetGame();

    while (1) {
        scanKeys();
        int held = keysHeld(), down = keysDown();
        frame++;
        if (state == 0) update(held, down);
        else if (down & KEY_START) resetGame();
        draw();
        swiWaitForVBlank();
        dmaCopy(buf, fb, sizeof(buf));
        hud();
    }
    return 0;
}
