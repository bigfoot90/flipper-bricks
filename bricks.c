// Bricks - puzzle a blocchi che cadono per Flipper Zero (display verticale)
// Musica: "Korobeiniki", canto popolare russo (pubblico dominio).

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <stdio.h>
#include <string.h>

// ---- Geometria (canvas verticale 64x128) ----
#define BW 10
#define BH 18
#define CELL 6
// Ogni cella occupa 5 px + 1 px di spazio a destra/sotto: la cornice sta a 2 px
// dalla prima cella, così resta 1 px di margine uguale su tutti i lati.
#define BX 2 // x della prima cella (cornice a x=0..62)
#define BY 19 // y della prima cella (cornice a y=17..127)

#define VOLUME 0.5f

#define CLEAR_MS 480 // durata animazione righe (6 fasi da 80 ms)
#define FLASH_MS 80
#define FAST_DROP_MS 30

// ---- Pezzi: 4 rotazioni, bitmask 4x4 (bit 0x8000 = riga 0, col 0) ----
static const uint16_t SHAPES[7][4] = {
    {0x0F00, 0x2222, 0x00F0, 0x4444}, // I
    {0x44C0, 0x8E00, 0x6440, 0x0E20}, // J
    {0x4460, 0x0E80, 0xC440, 0x2E00}, // L
    {0xCC00, 0xCC00, 0xCC00, 0xCC00}, // O
    {0x06C0, 0x8C40, 0x6C00, 0x4620}, // S
    {0x0E40, 0x4C40, 0x4E00, 0x4640}, // T
    {0x0C60, 0x4C80, 0xC600, 0x2640}, // Z
};

static inline bool shape_cell(uint8_t p, uint8_t rot, int r, int c) {
    return SHAPES[p][rot] & (0x8000 >> (r * 4 + c));
}

// ---- Melodia: Korobeiniki (durate in ottavi, freq 0 = pausa) ----
typedef struct {
    uint16_t freq;
    uint8_t len;
} Note;

#define A4 440
#define B4 494
#define C5 523
#define D5 587
#define E5 659
#define F5 698
#define G5 784
#define A5 880
#define GS4 415

static const Note MELODY[] = {
    // Parte A
    {E5, 2}, {B4, 1}, {C5, 1}, {D5, 2}, {C5, 1}, {B4, 1},
    {A4, 2}, {A4, 1}, {C5, 1}, {E5, 2}, {D5, 1}, {C5, 1},
    {B4, 3}, {C5, 1}, {D5, 2}, {E5, 2},
    {C5, 2}, {A4, 2}, {A4, 2}, {0, 2},
    {D5, 3}, {F5, 1}, {A5, 2}, {G5, 1}, {F5, 1},
    {E5, 3}, {C5, 1}, {E5, 2}, {D5, 1}, {C5, 1},
    {B4, 2}, {B4, 1}, {C5, 1}, {D5, 2}, {E5, 2},
    {C5, 2}, {A4, 2}, {A4, 2}, {0, 2},
    // Ripetizione parte A
    {E5, 2}, {B4, 1}, {C5, 1}, {D5, 2}, {C5, 1}, {B4, 1},
    {A4, 2}, {A4, 1}, {C5, 1}, {E5, 2}, {D5, 1}, {C5, 1},
    {B4, 3}, {C5, 1}, {D5, 2}, {E5, 2},
    {C5, 2}, {A4, 2}, {A4, 2}, {0, 2},
    {D5, 3}, {F5, 1}, {A5, 2}, {G5, 1}, {F5, 1},
    {E5, 3}, {C5, 1}, {E5, 2}, {D5, 1}, {C5, 1},
    {B4, 2}, {B4, 1}, {C5, 1}, {D5, 2}, {E5, 2},
    {C5, 2}, {A4, 2}, {A4, 2}, {0, 2},
    // Parte B (lenta)
    {E5, 4}, {C5, 4}, {D5, 4}, {B4, 4},
    {C5, 4}, {A4, 4}, {GS4, 4}, {B4, 4},
    {E5, 4}, {C5, 4}, {D5, 4}, {B4, 4},
    {C5, 2}, {E5, 2}, {A5, 4}, {GS4 * 2, 6}, {0, 2},
};
#define MELODY_LEN (sizeof(MELODY) / sizeof(MELODY[0]))

// ---- Stato ----
typedef enum {
    StateSplash,
    StateMenu,
    StatePlaying,
    StateClearing, // animazione delle righe completate
    StatePaused,
    StateOver,
} GameState;

typedef struct {
    uint8_t board[BH][BW];
    int8_t px, py;
    uint8_t piece, rot, next;
    uint8_t bag[7];
    uint8_t bag_i;
    uint32_t score, lines, level;
    GameState state;
    uint32_t last_fall;
    bool fast_drop; // GIÙ tenuto premuto
    uint32_t clear_mask; // bit r = riga r completa, in animazione
    uint32_t clear_start;
    uint32_t splash_start;

    // menu, record e salvataggi
    uint8_t menu_sel; // 0 = gioca/continua, 1 = suoni
    bool menu_new; // riga 0: false = "Continua", true = "Nuova"
    bool has_save; // c'è una partita salvata da riprendere
    bool save_ok; // esito dell'ultimo salvataggio (mostrato in pausa)
    uint32_t save_score;
    uint32_t best;
    bool new_record;
    bool ended; // fine partita già gestita

    // musica
    bool sound;
    bool speaker_ok;
    bool sounding;
    bool note_active;
    uint16_t note_i;
    uint32_t note_end;
    uint32_t gap_at;

    FuriMutex* mutex;
} Game;

// ---- Logica di gioco ----
static void bag_refill(Game* g) {
    for(uint8_t i = 0; i < 7; i++)
        g->bag[i] = i;
    for(int i = 6; i > 0; i--) { // Fisher-Yates
        int j = furi_hal_random_get() % (i + 1);
        uint8_t t = g->bag[i];
        g->bag[i] = g->bag[j];
        g->bag[j] = t;
    }
    g->bag_i = 0;
}

static uint8_t bag_next(Game* g) {
    if(g->bag_i >= 7) bag_refill(g);
    return g->bag[g->bag_i++];
}

static bool collides(Game* g, uint8_t p, uint8_t rot, int x, int y) {
    for(int r = 0; r < 4; r++)
        for(int c = 0; c < 4; c++) {
            if(!shape_cell(p, rot, r, c)) continue;
            int bx = x + c, by = y + r;
            if(bx < 0 || bx >= BW || by >= BH) return true;
            if(by >= 0 && g->board[by][bx]) return true;
        }
    return false;
}

static void spawn(Game* g) {
    g->piece = g->next;
    g->next = bag_next(g);
    g->rot = 0;
    g->px = 3;
    g->py = 0;
    g->fast_drop = false; // il nuovo pezzo non eredita la discesa rapida
    if(collides(g, g->piece, g->rot, g->px, g->py)) g->state = StateOver;
}

static uint32_t fall_ms(Game* g) {
    int32_t ms = 800 - (int32_t)(g->level - 1) * 70;
    return ms < 80 ? 80 : (uint32_t)ms;
}

static uint32_t full_rows(Game* g) {
    uint32_t mask = 0;
    for(int r = 0; r < BH; r++) {
        bool full = true;
        for(int c = 0; c < BW; c++)
            if(!g->board[r][c]) {
                full = false;
                break;
            }
        if(full) mask |= 1UL << r;
    }
    return mask;
}

// Rimuove le righe in clear_mask (dall'alto verso il basso: togliere la riga r
// sposta giù solo le righe sopra, quindi gli indici successivi restano validi)
static void remove_rows(Game* g) {
    static const uint16_t pts[5] = {0, 100, 300, 500, 800};
    int cleared = 0;
    for(int r = 0; r < BH; r++) {
        if(!(g->clear_mask & (1UL << r))) continue;
        memmove(&g->board[1][0], &g->board[0][0], r * BW);
        memset(&g->board[0][0], 0, BW);
        cleared++;
    }
    g->clear_mask = 0;
    if(cleared) {
        g->score += pts[cleared] * g->level;
        g->lines += cleared;
        g->level = g->lines / 10 + 1;
    }
}

// Conclude subito l'animazione delle righe (usato a fine animazione e prima di salvare)
static void finish_clearing(Game* g) {
    remove_rows(g);
    g->state = StatePlaying;
    spawn(g); // può portare a StateOver
}

static void lock_piece(Game* g) {
    for(int r = 0; r < 4; r++)
        for(int c = 0; c < 4; c++) {
            if(!shape_cell(g->piece, g->rot, r, c)) continue;
            int by = g->py + r;
            if(by < 0) {
                g->state = StateOver;
                return;
            }
            g->board[by][g->px + c] = 1;
        }
    g->clear_mask = full_rows(g);
    if(g->clear_mask) {
        g->state = StateClearing; // spawn dopo l'animazione
        g->clear_start = furi_get_tick();
    } else {
        spawn(g);
    }
}

static void try_move(Game* g, int dx) {
    if(!collides(g, g->piece, g->rot, g->px + dx, g->py)) g->px += dx;
}

static void try_rotate(Game* g) {
    uint8_t nr = (g->rot + 1) % 4;
    static const int8_t kicks[] = {0, -1, 1, -2, 2};
    for(size_t i = 0; i < sizeof(kicks); i++) {
        if(!collides(g, g->piece, nr, g->px + kicks[i], g->py)) {
            g->px += kicks[i];
            g->rot = nr;
            return;
        }
    }
}

static void soft_drop(Game* g, uint32_t now) {
    if(!collides(g, g->piece, g->rot, g->px, g->py + 1)) {
        g->py++;
        g->score += 1;
    } else {
        lock_piece(g);
    }
    g->last_fall = now;
}

static void game_reset(Game* g) {
    memset(g->board, 0, sizeof(g->board));
    g->score = 0;
    g->lines = 0;
    g->level = 1;
    g->clear_mask = 0;
    g->new_record = false;
    g->ended = false;
    g->bag_i = 7;
    g->next = bag_next(g);
    g->state = StatePlaying;
    g->note_i = 0;
    g->note_active = false;
    g->last_fall = furi_get_tick();
    spawn(g);
}

// ---- Salvataggi ----
// Impostazioni e record: restano per sempre.
#define SETTINGS_PATH APP_DATA_PATH("bricks.save")
#define SETTINGS_MAGIC 0x31434C42u // "BLC1"

typedef struct {
    uint32_t magic;
    uint8_t sound; // 0 = mai salvato (default attivo), 1 = attivi, 2 = disattivati
    uint8_t reserved[3];
    uint32_t best;
} Settings;

static void settings_load(Game* g) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    Settings s;
    memset(&s, 0, sizeof(s));
    if(storage_file_open(file, SETTINGS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        if(storage_file_read(file, &s, sizeof(s)) == sizeof(s) && s.magic == SETTINGS_MAGIC) {
            g->sound = s.sound != 2;
            g->best = s.best;
        }
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

static void settings_store(const Game* g) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    Settings s;
    memset(&s, 0, sizeof(s));
    s.magic = SETTINGS_MAGIC;
    s.sound = g->sound ? 1 : 2;
    s.best = g->best;
    if(storage_file_open(file, SETTINGS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_write(file, &s, sizeof(s));
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

// Partita in corso: salvata in pausa e all'uscita, cancellata a fine partita.
#define GAME_PATH APP_DATA_PATH("bricks.game")
#define GAME_MAGIC 0x47434C42u // "BLCG"
#define GAME_VERSION 1

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t note_i; // la musica riprende da dove era
    int8_t px, py;
    uint8_t piece, rot, next, bag_i;
    uint8_t bag[7];
    uint8_t reserved;
    uint32_t score, lines, level;
    uint8_t board[BH][BW];
} GameSave;

static bool game_save_valid(const GameSave* s) {
    if(s->magic != GAME_MAGIC || s->version != GAME_VERSION) return false;
    if(s->piece >= 7 || s->rot >= 4 || s->next >= 7 || s->bag_i > 7) return false;
    if(s->px < -3 || s->px >= BW || s->py < -3 || s->py >= BH) return false;
    if(s->level < 1 || s->note_i >= MELODY_LEN) return false;
    for(int i = 0; i < 7; i++)
        if(s->bag[i] >= 7) return false;
    for(int r = 0; r < BH; r++)
        for(int c = 0; c < BW; c++)
            if(s->board[r][c] > 1) return false;
    return true;
}

static bool game_save_read(GameSave* s) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok = false;
    if(storage_file_open(file, GAME_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        ok = storage_file_read(file, s, sizeof(*s)) == sizeof(*s) && game_save_valid(s);
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

static void game_save_write(Game* g) {
    GameSave s;
    memset(&s, 0, sizeof(s));
    s.magic = GAME_MAGIC;
    s.version = GAME_VERSION;
    s.note_i = g->note_i;
    s.px = g->px;
    s.py = g->py;
    s.piece = g->piece;
    s.rot = g->rot;
    s.next = g->next;
    s.bag_i = g->bag_i;
    memcpy(s.bag, g->bag, sizeof(s.bag));
    s.score = g->score;
    s.lines = g->lines;
    s.level = g->level;
    memcpy(s.board, g->board, sizeof(s.board));

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    bool ok = false;
    if(storage_file_open(file, GAME_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        ok = storage_file_write(file, &s, sizeof(s)) == sizeof(s);
    }
    storage_file_close(file);
    storage_file_free(file);
    if(!ok) storage_simply_remove(storage, GAME_PATH);
    furi_record_close(RECORD_STORAGE);
    g->has_save = ok;
    g->save_ok = ok;
    g->save_score = g->score;
}

static void game_save_delete(Game* g) {
    if(!g->has_save) return;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_remove(storage, GAME_PATH);
    furi_record_close(RECORD_STORAGE);
    g->has_save = false;
}

// Carica la partita salvata; la riprende in pausa (OK per ripartire)
static bool game_save_load(Game* g) {
    GameSave s;
    if(!game_save_read(&s)) return false;
    memcpy(g->board, s.board, sizeof(g->board));
    g->px = s.px;
    g->py = s.py;
    g->piece = s.piece;
    g->rot = s.rot;
    g->next = s.next;
    g->bag_i = s.bag_i;
    memcpy(g->bag, s.bag, sizeof(g->bag));
    g->score = s.score;
    g->lines = s.lines;
    g->level = s.level;
    g->note_i = s.note_i;
    g->note_active = false;
    g->clear_mask = 0;
    g->fast_drop = false;
    g->new_record = false;
    g->ended = false;
    g->save_ok = true;
    g->state = StatePaused;
    return true;
}

// Fine partita: aggiorna il record, non c'è più niente da riprendere
static void game_end(Game* g) {
    g->ended = true;
    if(g->score > g->best) {
        g->best = g->score;
        g->new_record = true;
    }
    game_save_delete(g);
    settings_store(g);
}

// Salva la partita se ce n'è una in corso (prima chiude l'animazione delle righe)
static void save_if_in_game(Game* g) {
    if(g->state == StateClearing) finish_clearing(g);
    if(g->state == StatePlaying || g->state == StatePaused) {
        game_save_write(g);
    } else if(g->state == StateOver && !g->ended) {
        game_end(g);
    }
}

// ---- Musica (non bloccante, avanzata dal loop principale) ----
static void speaker_off(Game* g) {
    if(g->sounding && g->speaker_ok) furi_hal_speaker_stop();
    g->sounding = false;
}

static void music_tick(Game* g, uint32_t now) {
    bool active = g->state == StatePlaying || g->state == StateClearing;
    if(!g->sound || !active || !g->speaker_ok) {
        speaker_off(g);
        g->note_active = false;
        return;
    }
    if(!g->note_active || (int32_t)(now - g->note_end) >= 0) {
        const Note* n = &MELODY[g->note_i];
        g->note_i = (g->note_i + 1) % MELODY_LEN;
        // il tempo accelera leggermente con il livello
        int32_t eighth = 190 - (int32_t)(g->level - 1) * 8;
        if(eighth < 110) eighth = 110;
        uint32_t dur = n->len * (uint32_t)eighth;
        if(n->freq) {
            furi_hal_speaker_start(n->freq, VOLUME);
            g->sounding = true;
        } else {
            speaker_off(g);
        }
        g->note_end = now + dur;
        g->gap_at = now + dur - dur / 8; // piccolo stacco tra le note
        g->note_active = true;
    } else if(g->sounding && (int32_t)(now - g->gap_at) >= 0) {
        speaker_off(g);
    }
}

// ---- Disegno ----
static void draw_cell(Canvas* canvas, int c, int r, bool ghost) {
    int x = BX + c * CELL, y = BY + r * CELL;
    if(ghost)
        canvas_draw_frame(canvas, x, y, CELL - 1, CELL - 1);
    else
        canvas_draw_box(canvas, x, y, CELL - 1, CELL - 1);
}

static void draw_overlay(Canvas* canvas, const char* title, const char* const* lines, int n) {
    int h = 18 + n * 10;
    int y0 = 64 - h / 2;
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, 2, y0, 60, h);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_frame(canvas, 2, y0, 60, h);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 32, y0 + 3, AlignCenter, AlignTop, title);
    canvas_set_font(canvas, FontSecondary);
    for(int i = 0; i < n; i++)
        canvas_draw_str_aligned(canvas, 32, y0 + 17 + i * 10, AlignCenter, AlignTop, lines[i]);
}

// ---- Splash screen ----
#define SPLASH_MS 3500
#define LETTER_DELAY_MS 120 // ritardo tra una lettera e la successiva
#define LETTER_FALL_MS 400 // durata della caduta di una lettera
#define LETTER_FALL_PX 40

// Lettere alte 5 a larghezza variabile: ogni riga è un valore a w bit,
// il bit più alto è la colonna di sinistra. La I è larga 1 così le 6 lettere
// stanno in 21 colonne x 3 px = 62 px.
#define TITLE_LEN 6
static const struct {
    uint8_t w;
    uint8_t rows[5];
} TITLE_FONT[TITLE_LEN] = {
    {3, {6, 5, 6, 5, 6}}, // B
    {3, {6, 5, 6, 5, 5}}, // R
    {1, {1, 1, 1, 1, 1}}, // I
    {3, {3, 4, 4, 4, 3}}, // C
    {3, {5, 5, 6, 5, 5}}, // K
    {3, {3, 4, 2, 1, 6}}, // S
};

// Mattoncino grande del logo, 12x12: cornice spessa 2 px e quadrato pieno al centro
static void draw_logo_block(Canvas* canvas, int x, int y) {
    canvas_draw_box(canvas, x, y, 12, 12);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, x + 2, y + 2, 8, 8);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, x + 4, y + 4, 4, 4);
}

static void draw_splash(Canvas* canvas, Game* g) {
    uint32_t t = furi_get_tick() - g->splash_start;

    // Logo: il pezzo a T, con mattoncini grandi (passo 13 px)
    for(int r = 0; r < 2; r++)
        for(int c = 0; c < 3; c++)
            if(shape_cell(5, 2, r, c)) draw_logo_block(canvas, 13 + c * 13, 12 + r * 13);

    // Titolo "BRICKS" fatto di mattoncini 2x2 (passo 3 px); le lettere cadono una alla volta
    const int y0 = 54;
    int lx = 1; // colonna (in px) della lettera corrente
    for(int i = 0; i < TITLE_LEN; i++) {
        uint8_t w = TITLE_FONT[i].w;
        int32_t lt = (int32_t)t - i * LETTER_DELAY_MS;
        if(lt >= 0) {
            int dy = lt >= LETTER_FALL_MS ? 0 :
                                            LETTER_FALL_PX - lt * LETTER_FALL_PX / LETTER_FALL_MS;
            for(int r = 0; r < 5; r++)
                for(int c = 0; c < w; c++)
                    if(TITLE_FONT[i].rows[r] & (1 << (w - 1 - c)))
                        canvas_draw_box(canvas, lx + c * 3, y0 - dy + r * 3, 2, 2);
        }
        lx += (w + 1) * 3; // larghezza della lettera + 1 colonna di spazio
    }

    canvas_set_font(canvas, FontSecondary);
    if(t > 1000 && (t / 500) % 2 == 0)
        canvas_draw_str_aligned(canvas, 32, 112, AlignCenter, AlignTop, "Premi OK");
}

// pezzo decorativo per il menu, celle da 5 px
static void draw_menu_piece(Canvas* canvas, uint8_t p, uint8_t rot, int x, int y) {
    for(int r = 0; r < 4; r++)
        for(int c = 0; c < 4; c++)
            if(shape_cell(p, rot, r, c)) canvas_draw_box(canvas, x + c * 6, y + r * 6, 5, 5);
}

static void draw_menu(Canvas* canvas, Game* g) {
    char buf[24];
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 32, 2, AlignCenter, AlignTop, "BRICKS");

    draw_menu_piece(canvas, 5, 0, 2, 12); // T
    draw_menu_piece(canvas, 3, 0, 26, 18); // O
    draw_menu_piece(canvas, 2, 1, 40, 12); // L

    canvas_set_font(canvas, FontSecondary);
    const char* items[2];
    const char* play = g->has_save ? (g->menu_new ? "Nuova" : "Continua") : "Gioca";
    char sound[16];
    snprintf(sound, sizeof(sound), "Suoni: %s", g->sound ? "Si'" : "No");
    items[0] = play;
    items[1] = sound;
    for(int i = 0; i < 2; i++) {
        int y = 48 + i * 14;
        bool sel = g->menu_sel == i;
        if(sel) {
            canvas_draw_box(canvas, 1, y, 62, 12);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str_aligned(canvas, 32, y + 2, AlignCenter, AlignTop, items[i]);
        bool arrows = sel && (i == 1 || g->has_save);
        if(arrows) {
            canvas_draw_str(canvas, 3, y + 9, "<");
            canvas_draw_str(canvas, 57, y + 9, ">");
        }
        canvas_set_color(canvas, ColorBlack);
    }

    if(g->has_save) {
        canvas_draw_str_aligned(canvas, 32, 84, AlignCenter, AlignTop, "Salvata");
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)g->save_score);
        canvas_draw_str_aligned(canvas, 32, 93, AlignCenter, AlignTop, buf);
    }
    canvas_draw_line(canvas, 8, 105, 55, 105);
    canvas_draw_str_aligned(canvas, 32, 108, AlignCenter, AlignTop, "Record");
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)g->best);
    canvas_draw_str_aligned(canvas, 32, 117, AlignCenter, AlignTop, buf);
}

static void draw_callback(Canvas* canvas, void* ctx) {
    Game* g = ctx;
    furi_mutex_acquire(g->mutex, FuriWaitForever);
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    if(g->state == StateSplash || g->state == StateMenu) {
        if(g->state == StateSplash)
            draw_splash(canvas, g);
        else
            draw_menu(canvas, g);
        furi_mutex_release(g->mutex);
        return;
    }

    char buf[24];

    // Intestazione
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)g->score);
    canvas_draw_str(canvas, 1, 8, buf);
    snprintf(buf, sizeof(buf), "Lv%lu R%lu", (unsigned long)g->level, (unsigned long)g->lines);
    canvas_draw_str(canvas, 1, 16, buf);

    // Prossimo pezzo (celle da 3 px)
    for(int r = 0; r < 4; r++)
        for(int c = 0; c < 4; c++)
            if(shape_cell(g->next, 0, r, c)) canvas_draw_box(canvas, 51 + c * 3, 2 + r * 3, 3, 3);

    // Campo
    canvas_draw_frame(canvas, BX - 2, BY - 2, BW * CELL + 3, BH * CELL + 3);
    bool flash_on = ((furi_get_tick() - g->clear_start) / FLASH_MS) % 2 == 0;
    for(int r = 0; r < BH; r++) {
        if(g->clear_mask & (1UL << r)) {
            // riga completata: barra piena che lampeggia
            if(flash_on) canvas_draw_box(canvas, BX, BY + r * CELL, BW * CELL - 1, CELL - 1);
            continue;
        }
        for(int c = 0; c < BW; c++)
            if(g->board[r][c]) draw_cell(canvas, c, r, false);
    }

    if(g->state == StatePlaying || g->state == StatePaused) {
        // Pezzo "fantasma" (dove atterrerà)
        int gy = g->py;
        while(!collides(g, g->piece, g->rot, g->px, gy + 1))
            gy++;
        for(int r = 0; r < 4; r++)
            for(int c = 0; c < 4; c++) {
                if(!shape_cell(g->piece, g->rot, r, c)) continue;
                if(gy + r >= 0) draw_cell(canvas, g->px + c, gy + r, true);
                if(g->py + r >= 0) draw_cell(canvas, g->px + c, g->py + r, false);
            }
    }

    if(g->state == StatePaused) {
        const char* l[] = {g->save_ok ? "Salvata" : "Errore salv.", "OK: gioca", "Back: menu"};
        draw_overlay(canvas, "PAUSA", l, 3);
    } else if(g->state == StateOver) {
        char rec[24];
        snprintf(buf, sizeof(buf), "Punti %lu", (unsigned long)g->score);
        if(g->new_record)
            snprintf(rec, sizeof(rec), "Nuovo rec.!");
        else
            snprintf(rec, sizeof(rec), "Rec. %lu", (unsigned long)g->best);
        const char* l[] = {buf, rec, "OK: rigioca", "Back: menu"};
        draw_overlay(canvas, "GAME OVER", l, 4);
    }
    furi_mutex_release(g->mutex);
}

static void input_callback(InputEvent* ev, void* ctx) {
    furi_message_queue_put((FuriMessageQueue*)ctx, ev, FuriWaitForever);
}

static void new_game(Game* g) {
    game_save_delete(g);
    game_reset(g);
}

static void go_to_menu(Game* g) {
    g->state = StateMenu;
    g->menu_sel = 0;
    g->menu_new = false;
}

// ritorna false per uscire dall'app
static bool handle_input(Game* g, const InputEvent* ev, uint32_t now) {
    bool press = ev->type == InputTypePress;
    bool rep = press || ev->type == InputTypeRepeat;
    bool shrt = ev->type == InputTypeShort;

    if(ev->key == InputKeyDown && ev->type == InputTypeRelease) g->fast_drop = false;

    // Back tenuto premuto: esce da qualsiasi schermata, salvando la partita
    if(ev->key == InputKeyBack && ev->type == InputTypeLong) {
        save_if_in_game(g);
        settings_store(g);
        return false;
    }

    switch(g->state) {
    case StateSplash:
        if(shrt && ev->key == InputKeyBack) return false;
        if(shrt) g->state = StateMenu; // qualsiasi tasto salta lo splash
        break;

    case StateMenu:
        if(rep && (ev->key == InputKeyUp || ev->key == InputKeyDown)) {
            g->menu_sel ^= 1;
        } else if(rep && (ev->key == InputKeyLeft || ev->key == InputKeyRight)) {
            if(g->menu_sel == 0) {
                if(g->has_save) g->menu_new = !g->menu_new;
            } else {
                g->sound = !g->sound;
                settings_store(g);
            }
        } else if(shrt && ev->key == InputKeyOk) {
            if(g->menu_sel == 1) {
                g->sound = !g->sound;
                settings_store(g);
            } else if(g->has_save && !g->menu_new) {
                if(!game_save_load(g)) new_game(g); // file rovinato: nuova partita
            } else {
                new_game(g);
            }
        } else if(shrt && ev->key == InputKeyBack) {
            settings_store(g);
            return false;
        }
        break;

    case StatePlaying:
        if(ev->key == InputKeyLeft && rep)
            try_move(g, -1);
        else if(ev->key == InputKeyRight && rep)
            try_move(g, 1);
        else if(ev->key == InputKeyDown && press) { // discesa rapida finché tenuto
            g->fast_drop = true;
            soft_drop(g, now);
        } else if((ev->key == InputKeyOk || ev->key == InputKeyUp) && press)
            try_rotate(g);
        else if(ev->key == InputKeyBack && shrt) {
            g->state = StatePaused;
            g->fast_drop = false;
            game_save_write(g); // salvataggio automatico in pausa
        }
        break;

    case StateClearing:
        break; // mezzo secondo di animazione: si aspetta

    case StatePaused:
        if(ev->key == InputKeyOk && shrt) {
            g->state = StatePlaying;
            g->last_fall = now;
        } else if(ev->key == InputKeyBack && shrt) {
            go_to_menu(g); // la partita resta salvata: "Continua" dal menu
        }
        break;

    case StateOver:
        if(ev->key == InputKeyOk && shrt)
            new_game(g);
        else if(ev->key == InputKeyBack && shrt)
            go_to_menu(g);
        break;
    }
    return true;
}

// ---- Entry point ----
int32_t bricks_app(void* p) {
    UNUSED(p);
    Game* g = malloc(sizeof(Game));
    memset(g, 0, sizeof(Game));
    g->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    g->sound = true;
    g->level = 1;
    settings_load(g);
    {
        GameSave s;
        g->has_save = game_save_read(&s);
        if(g->has_save) g->save_score = s.score;
    }
    g->state = StateSplash;
    g->splash_start = furi_get_tick();
    g->speaker_ok = furi_hal_speaker_acquire(1000);

    NotificationApp* notif = furi_record_open(RECORD_NOTIFICATION);
    notification_message(notif, &sequence_display_backlight_enforce_on);

    FuriMessageQueue* queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    ViewPort* vp = view_port_alloc();
    view_port_set_orientation(vp, ViewPortOrientationVertical);
    view_port_draw_callback_set(vp, draw_callback, g);
    view_port_input_callback_set(vp, input_callback, queue);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, vp, GuiLayerFullscreen);

    bool running = true;
    while(running) {
        InputEvent ev;
        bool got = furi_message_queue_get(queue, &ev, 10) == FuriStatusOk;
        furi_mutex_acquire(g->mutex, FuriWaitForever);
        uint32_t now = furi_get_tick();

        if(got) running = handle_input(g, &ev, now);

        if(running) {
            if(g->state == StateSplash && now - g->splash_start >= SPLASH_MS)
                g->state = StateMenu;

            // Gravità
            if(g->state == StatePlaying) {
                uint32_t interval = g->fast_drop ? FAST_DROP_MS : fall_ms(g);
                if(now - g->last_fall >= interval) {
                    if(!collides(g, g->piece, g->rot, g->px, g->py + 1)) {
                        g->py++;
                        if(g->fast_drop) g->score += 1;
                    } else {
                        lock_piece(g);
                    }
                    g->last_fall = now;
                }
            } else if(g->state == StateClearing && now - g->clear_start >= CLEAR_MS) {
                finish_clearing(g);
                g->last_fall = now;
            }

            if(g->state == StateOver && !g->ended) game_end(g);

            music_tick(g, now);
        }
        furi_mutex_release(g->mutex);
        view_port_update(vp);
    }

    speaker_off(g);
    if(g->speaker_ok) furi_hal_speaker_release();
    notification_message(notif, &sequence_display_backlight_enforce_auto);
    furi_record_close(RECORD_NOTIFICATION);
    view_port_enabled_set(vp, false);
    gui_remove_view_port(gui, vp);
    view_port_free(vp);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(queue);
    furi_mutex_free(g->mutex);
    free(g);
    return 0;
}
