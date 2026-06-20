/*****************************************************************************
 *  DSLR Camera Simulator — Interactive explainer built with Raylib + WASM
 *
 *  A 2D interactive DSLR camera where users can:
 *    - Click on camera parts to learn what they do
 *    - Adjust aperture, shutter speed, and ISO
 *    - See a live viewfinder showing how settings affect the image
 *
 *  Compiled to WebAssembly via Emscripten for browser embedding.
 *****************************************************************************/

#include "raylib.h"
#include "raymath.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#ifdef PLATFORM_WEB
#include <emscripten/emscripten.h>
#endif

/* ── Constants ─────────────────────────────────────────────────────────── */
#define SCREEN_W 960
#define SCREEN_H 640

/* Camera body geometry */
#define BODY_X 60
#define BODY_Y 160
#define BODY_W 420
#define BODY_H 280

/* Lens geometry */
#define LENS_CX 230
#define LENS_CY 300
#define LENS_R_OUTER 110
#define LENS_R_INNER 70
#define LENS_R_GLASS  50

/* Viewfinder panel */
#define VF_X 530
#define VF_Y 160
#define VF_W 390
#define VF_H 240

/* Info panel */
#define INFO_X 530
#define INFO_Y 420
#define INFO_W 390
#define INFO_H 180

/* Colors */
#define COL_BG        (Color){15, 15, 23, 255}
#define COL_BODY      (Color){45, 45, 55, 255}
#define COL_BODY_TOP  (Color){55, 55, 68, 255}
#define COL_DARK      (Color){25, 25, 35, 255}
#define COL_ACCENT    (Color){59, 130, 246, 255}
#define COL_AMBER     (Color){245, 158, 11, 255}
#define COL_GREEN     (Color){16, 185, 129, 255}
#define COL_PURPLE    (Color){139, 92, 246, 255}
#define COL_TEXT      (Color){226, 232, 240, 255}
#define COL_DIM       (Color){136, 146, 164, 255}
#define COL_PANEL     (Color){18, 18, 26, 255}
#define COL_BORDER    (Color){42, 45, 58, 255}
#define COL_HOTSPOT   (Color){59, 130, 246, 60}
#define COL_LCD       (Color){20, 40, 20, 255}
#define COL_LCD_TEXT  (Color){120, 200, 80, 255}

/* ── Hotspot / clickable region ────────────────────────────────────────── */
typedef struct {
    Rectangle rect;
    int is_circle;          /* 1 = circle (rect defines bounding box) */
    const char *label;
    const char *title;
    const char *line1;
    const char *line2;
    const char *line3;
    Color highlight;
} Hotspot;

/* ── Camera settings ──────────────────────────────────────────────────── */
static float apertures[]   = {1.4f, 2.0f, 2.8f, 4.0f, 5.6f, 8.0f, 11.0f, 16.0f, 22.0f};
static const char *shutter_labels[] = {"1/8000","1/4000","1/2000","1/1000","1/500","1/250","1/125","1/60","1/30","1/15"};
static float shutter_vals[] = {8000,4000,2000,1000,500,250,125,60,30,15};
static int iso_vals[] = {100, 200, 400, 800, 1600, 3200, 6400, 12800};

static int apt_idx = 4;     /* f/5.6 */
static int shtr_idx = 5;    /* 1/250 */
static int iso_idx = 1;     /* 200 */

#define NUM_APERTURES  9
#define NUM_SHUTTERS  10
#define NUM_ISOS       8

/* ── State ─────────────────────────────────────────────────────────────── */
static int active_hotspot = -1;
static float info_alpha = 0.0f;
static float vf_noise_seed = 0.0f;

/* ── Hotspot definitions ──────────────────────────────────────────────── */
#define NUM_HOTSPOTS 9

static Hotspot hotspots[NUM_HOTSPOTS];

static void init_hotspots(void)
{
    /* 0: Lens */
    hotspots[0] = (Hotspot){
        .rect = {LENS_CX - LENS_R_OUTER, LENS_CY - LENS_R_OUTER,
                 LENS_R_OUTER*2, LENS_R_OUTER*2},
        .is_circle = 1,
        .label = "LENS",
        .title = "The Lens \xe2\x80\x94 Pure Optics",
        .line1 = "Precisely shaped glass elements bend light onto the sensor.",
        .line2 = "The aperture iris controls exposure & depth of field.",
        .line3 = "Everything here is analog: photons through glass.",
        .highlight = COL_AMBER
    };
    /* 1: Sensor (behind the lens, shown as inner area) */
    hotspots[1] = (Hotspot){
        .rect = {LENS_CX - 30, LENS_CY - 30, 60, 60},
        .is_circle = 1,
        .label = "SENSOR",
        .title = "CMOS Sensor \xe2\x80\x94 Photons to Electrons",
        .line1 = "Millions of photosites convert light into electrical charge.",
        .line2 = "Bayer filter: each site sees only R, G, or B.",
        .line3 = "Output is a mosaic of voltages, not an image yet.",
        .highlight = COL_AMBER
    };
    /* 2: Top LCD */
    hotspots[2] = (Hotspot){
        .rect = {BODY_X + 230, BODY_Y - 30, 140, 28},
        .is_circle = 0,
        .label = "LCD",
        .title = "Top LCD \xe2\x80\x94 Settings at a Glance",
        .line1 = "Displays aperture, shutter speed, ISO, and shots remaining.",
        .line2 = "Low-power reflective display visible in direct sunlight.",
        .line3 = "No battery drain: always on, always readable.",
        .highlight = COL_GREEN
    };
    /* 3: Hot shoe */
    hotspots[3] = (Hotspot){
        .rect = {BODY_X + 170, BODY_Y - 48, 70, 20},
        .is_circle = 0,
        .label = "HOT SHOE",
        .title = "Hot Shoe \xe2\x80\x94 Accessory Mount",
        .line1 = "Standard mount for flash units, microphones, monitors.",
        .line2 = "Center pin fires the flash; outer contacts carry TTL data.",
        .line3 = "Provides both physical mount and electrical connection.",
        .highlight = COL_PURPLE
    };
    /* 4: Viewfinder / EVF */
    hotspots[4] = (Hotspot){
        .rect = {BODY_X + 170, BODY_Y + 10, 70, 50},
        .is_circle = 0,
        .label = "EVF",
        .title = "Electronic Viewfinder",
        .line1 = "Tiny OLED display showing the live sensor feed.",
        .line2 = "What you see is what you get: exposure, WB, focus peaking.",
        .line3 = "Replaces the optical viewfinder + mirror of older DSLRs.",
        .highlight = COL_ACCENT
    };
    /* 5: Card slot */
    hotspots[5] = (Hotspot){
        .rect = {BODY_X + BODY_W - 55, BODY_Y + 80, 45, 80},
        .is_circle = 0,
        .label = "CARD",
        .title = "Memory Card Slot \xe2\x80\x94 Portable SSD",
        .line1 = "CFexpress/SD: miniaturized SSDs with standardized form factor.",
        .line2 = "Write speed is the recording bottleneck, not the processor.",
        .line3 = "Filesystem: exFAT. Folder structure: DCIM/ standard.",
        .highlight = COL_GREEN
    };
    /* 6: Grip / battery */
    hotspots[6] = (Hotspot){
        .rect = {BODY_X, BODY_Y + 100, 60, 180},
        .is_circle = 0,
        .label = "GRIP",
        .title = "Grip & Battery Compartment",
        .line1 = "Ergonomic grip houses the lithium-ion battery.",
        .line2 = "Battery powers sensor, ISP, EVF, card writes, and AF motors.",
        .line3 = "Typical capacity: 2,000-3,000 shots per charge.",
        .highlight = COL_DIM
    };
    /* 7: Rear dial area */
    hotspots[7] = (Hotspot){
        .rect = {BODY_X + BODY_W - 80, BODY_Y + 170, 70, 70},
        .is_circle = 1,
        .label = "DIAL",
        .title = "Control Dial \xe2\x80\x94 Parameter Adjustment",
        .line1 = "Rotary encoder for changing aperture, shutter, or ISO.",
        .line2 = "Tactile clicks give blind-adjustable feedback.",
        .line3 = "Context-sensitive: function changes with selected mode.",
        .highlight = COL_ACCENT
    };
    /* 8: Lens mount ring */
    hotspots[8] = (Hotspot){
        .rect = {LENS_CX - LENS_R_OUTER - 8, LENS_CY - LENS_R_OUTER - 8,
                 (LENS_R_OUTER+8)*2, (LENS_R_OUTER+8)*2},
        .is_circle = 1,
        .label = "MOUNT",
        .title = "Lens Mount \xe2\x80\x94 Bayonet Interface",
        .line1 = "Precision bayonet mount with electronic contact pins.",
        .line2 = "Pins communicate AF motor control, aperture, and lens ID.",
        .line3 = "Flange distance determines which lenses are compatible.",
        .highlight = COL_DIM
    };
}

/* ── Utility ───────────────────────────────────────────────────────────── */
static bool point_in_circle(Vector2 p, float cx, float cy, float r)
{
    float dx = p.x - cx;
    float dy = p.y - cy;
    return (dx*dx + dy*dy) <= r*r;
}

static int hit_test(Vector2 mouse)
{
    /* Test in reverse order so smaller/overlapping regions take priority */
    /* Sensor (1) is inside lens (0) — test sensor first */
    for (int i = 0; i < NUM_HOTSPOTS; i++) {
        /* Skip mount ring (8) if lens (0) or sensor (1) already hit */
        Hotspot *h = &hotspots[i];
        if (h->is_circle) {
            float cx = h->rect.x + h->rect.width / 2;
            float cy = h->rect.y + h->rect.height / 2;
            float r  = h->rect.width / 2;
            if (point_in_circle(mouse, cx, cy, r)) return i;
        } else {
            if (CheckCollisionPointRec(mouse, h->rect)) return i;
        }
    }
    return -1;
}

/* ── Drawing: Camera body ──────────────────────────────────────────────── */
static void draw_camera(void)
{
    /* Grip (slightly taller than body) */
    DrawRectangleRounded((Rectangle){BODY_X - 5, BODY_Y + 20, 70, BODY_H + 10},
                         0.15f, 8, COL_BODY);
    /* Grip texture lines */
    for (int i = 0; i < 8; i++) {
        int gy = BODY_Y + 130 + i * 12;
        DrawLine(BODY_X + 5, gy, BODY_X + 50, gy, (Color){60, 60, 75, 255});
    }

    /* Main body */
    DrawRectangleRounded((Rectangle){BODY_X + 40, BODY_Y, BODY_W - 40, BODY_H},
                         0.06f, 8, COL_BODY);

    /* Top prism housing */
    DrawRectangleRounded((Rectangle){BODY_X + 130, BODY_Y - 50, 160, 55},
                         0.15f, 8, COL_BODY_TOP);

    /* Hot shoe */
    DrawRectangle(BODY_X + 175, BODY_Y - 48, 60, 8, (Color){80, 80, 95, 255});
    DrawRectangle(BODY_X + 195, BODY_Y - 48, 20, 5, (Color){180, 160, 60, 255});

    /* EVF eyepiece */
    DrawRectangleRounded((Rectangle){BODY_X + 180, BODY_Y + 12, 50, 45},
                         0.2f, 6, COL_DARK);
    DrawRectangleRounded((Rectangle){BODY_X + 185, BODY_Y + 17, 40, 35},
                         0.15f, 6, (Color){10, 10, 15, 255});

    /* Top LCD panel */
    DrawRectangleRounded((Rectangle){BODY_X + 232, BODY_Y - 28, 136, 24},
                         0.3f, 6, COL_LCD);

    /* LCD content */
    char lcd_text[64];
    snprintf(lcd_text, sizeof(lcd_text), "f/%.1f  %s  ISO%d",
             apertures[apt_idx], shutter_labels[shtr_idx], iso_vals[iso_idx]);
    DrawText(lcd_text, BODY_X + 240, BODY_Y - 24, 10, COL_LCD_TEXT);

    /* Card slot (right side) */
    DrawRectangleRounded((Rectangle){BODY_X + BODY_W - 52, BODY_Y + 85, 40, 72},
                         0.1f, 4, COL_DARK);
    DrawRectangle(BODY_X + BODY_W - 45, BODY_Y + 92, 26, 58, (Color){35, 35, 50, 255});
    DrawText("CF", BODY_X + BODY_W - 40, BODY_Y + 112, 10, COL_DIM);

    /* Rear control dial */
    float dial_cx = BODY_X + BODY_W - 45;
    float dial_cy = BODY_Y + 205;
    DrawCircle(dial_cx, dial_cy, 30, COL_DARK);
    DrawCircleLines(dial_cx, dial_cy, 30, COL_BORDER);
    /* Dial notches */
    for (int i = 0; i < 12; i++) {
        float angle = i * 30.0f * DEG2RAD;
        float x1 = dial_cx + cosf(angle) * 24;
        float y1 = dial_cy + sinf(angle) * 24;
        float x2 = dial_cx + cosf(angle) * 28;
        float y2 = dial_cy + sinf(angle) * 28;
        DrawLineEx((Vector2){x1,y1}, (Vector2){x2,y2}, 1.5f, COL_DIM);
    }

    /* Shutter button (top right of body) */
    DrawCircle(BODY_X + BODY_W - 80, BODY_Y - 10, 16, (Color){70, 70, 85, 255});
    DrawCircle(BODY_X + BODY_W - 80, BODY_Y - 10, 12, (Color){90, 90, 108, 255});

    /* Mode dial (top left) */
    DrawCircle(BODY_X + 100, BODY_Y - 12, 22, (Color){60, 60, 75, 255});
    DrawCircleLines(BODY_X + 100, BODY_Y - 12, 22, COL_BORDER);
    DrawText("M", BODY_X + 95, BODY_Y - 18, 10, COL_TEXT);

    /* Lens mount ring */
    DrawCircleLines(LENS_CX, LENS_CY, LENS_R_OUTER + 5, (Color){70, 70, 85, 255});
    DrawCircleLines(LENS_CX, LENS_CY, LENS_R_OUTER + 3, (Color){55, 55, 68, 255});

    /* Lens barrel (outer) */
    DrawCircle(LENS_CX, LENS_CY, LENS_R_OUTER, (Color){35, 35, 48, 255});

    /* Aperture ring markings */
    for (int i = 0; i < 16; i++) {
        float angle = i * 22.5f * DEG2RAD;
        float x1 = LENS_CX + cosf(angle) * (LENS_R_OUTER - 5);
        float y1 = LENS_CY + sinf(angle) * (LENS_R_OUTER - 5);
        float x2 = LENS_CX + cosf(angle) * (LENS_R_OUTER - 12);
        float y2 = LENS_CY + sinf(angle) * (LENS_R_OUTER - 12);
        DrawLineEx((Vector2){x1,y1}, (Vector2){x2,y2}, 1.0f, COL_DIM);
    }

    /* Focus ring */
    DrawCircle(LENS_CX, LENS_CY, LENS_R_INNER + 8, (Color){40, 40, 55, 255});
    DrawCircle(LENS_CX, LENS_CY, LENS_R_INNER, (Color){30, 30, 42, 255});

    /* Glass element (inner) */
    DrawCircle(LENS_CX, LENS_CY, LENS_R_GLASS, (Color){15, 18, 30, 255});
    /* Lens coating reflection */
    DrawCircle(LENS_CX - 12, LENS_CY - 12, 18, (Color){40, 50, 90, 40});
    DrawCircle(LENS_CX + 8, LENS_CY + 5, 10, (Color){60, 40, 80, 25});

    /* Aperture blades visualization */
    int num_blades = 9;
    float apt_radius = LENS_R_GLASS * (1.0f - apertures[apt_idx] / 30.0f);
    if (apt_radius < 8) apt_radius = 8;
    for (int i = 0; i < num_blades; i++) {
        float a1 = (i * 360.0f / num_blades) * DEG2RAD;
        float a2 = ((i + 1) * 360.0f / num_blades) * DEG2RAD;
        float mid = (a1 + a2) / 2.0f;
        Vector2 p1 = {LENS_CX + cosf(a1) * apt_radius, LENS_CY + sinf(a1) * apt_radius};
        Vector2 p2 = {LENS_CX + cosf(a2) * apt_radius, LENS_CY + sinf(a2) * apt_radius};
        Vector2 p3 = {LENS_CX + cosf(mid) * LENS_R_GLASS, LENS_CY + sinf(mid) * LENS_R_GLASS};
        DrawTriangle(p3, p2, p1, (Color){20, 22, 32, 230});
    }

    /* Sensor behind glass (tiny colored grid hint) */
    for (int gx = -2; gx <= 2; gx++) {
        for (int gy = -2; gy <= 2; gy++) {
            Color gc;
            if ((gx + gy) % 2 == 0) gc = (Color){0, 60, 0, 80};
            else if (gx % 2 == 0) gc = (Color){60, 0, 0, 80};
            else gc = (Color){0, 0, 60, 80};
            DrawRectangle(LENS_CX + gx*6 - 2, LENS_CY + gy*6 - 2, 5, 5, gc);
        }
    }
}

/* ── Drawing: Viewfinder simulation ────────────────────────────────────── */
static void draw_viewfinder(void)
{
    /* Panel background */
    DrawRectangleRounded((Rectangle){VF_X, VF_Y - 30, VF_W, 28}, 0.4f, 6, COL_PANEL);
    DrawText("VIEWFINDER", VF_X + 12, VF_Y - 26, 10, COL_DIM);

    /* Settings readout */
    char settings[80];
    snprintf(settings, sizeof(settings), "f/%.1f   %s   ISO %d",
             apertures[apt_idx], shutter_labels[shtr_idx], iso_vals[iso_idx]);
    int sw = MeasureText(settings, 10);
    DrawText(settings, VF_X + VF_W - sw - 12, VF_Y - 26, 10, COL_ACCENT);

    DrawRectangle(VF_X, VF_Y, VF_W, VF_H, (Color){5, 5, 8, 255});
    DrawRectangleLinesEx((Rectangle){VF_X, VF_Y, VF_W, VF_H}, 1, COL_BORDER);

    /* Simulate scene: sky gradient + ground + subject */
    /* Exposure = f(aperture, shutter, iso) */
    float ev = log2f(apertures[apt_idx] * apertures[apt_idx])
             - log2f(1.0f / shutter_vals[shtr_idx])
             - log2f(iso_vals[iso_idx] / 100.0f);
    /* ev ~= 0 at "correct" exposure for our scene */
    /* Map to brightness: lower ev = brighter */
    float brightness = 1.0f - (ev - 5.0f) / 12.0f;
    if (brightness < 0.05f) brightness = 0.05f;
    if (brightness > 2.0f) brightness = 2.0f;

    /* Noise from high ISO */
    float noise_level = 0.0f;
    if (iso_idx >= 4) noise_level = (iso_idx - 3) * 0.08f;

    /* Depth of field (aperture) — affects background blur */
    float dof_blur = apertures[apt_idx] / 22.0f; /* 0..1, higher = more in focus */

    /* Draw sky */
    for (int y = 0; y < VF_H * 0.55f; y++) {
        float t = (float)y / (VF_H * 0.55f);
        int r = (int)((30 + t * 60) * brightness);
        int g = (int)((50 + t * 80) * brightness);
        int b = (int)((100 + t * 120) * brightness);
        if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
        DrawLine(VF_X + 1, VF_Y + y, VF_X + VF_W - 1, VF_Y + y,
                 (Color){r, g, b, 255});
    }

    /* Ground / landscape */
    int ground_y = VF_Y + (int)(VF_H * 0.55f);
    for (int y = ground_y; y < VF_Y + VF_H; y++) {
        float t = (float)(y - ground_y) / (VF_Y + VF_H - ground_y);
        int r = (int)((25 + t * 20) * brightness);
        int g = (int)((50 + t * 30) * brightness);
        int b = (int)((20 + t * 10) * brightness);
        if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
        DrawLine(VF_X + 1, y, VF_X + VF_W - 1, y, (Color){r, g, b, 255});
    }

    /* Mountains (background — affected by DOF blur) */
    float bg_alpha_f = 255.0f * (0.3f + 0.7f * dof_blur);
    unsigned char bg_alpha = (bg_alpha_f > 255) ? 255 : (unsigned char)bg_alpha_f;
    int peaks[] = {40, 70, 55, 85, 60, 45, 75, 50, 65, 80, 55, 40, 70};
    for (int i = 0; i < 12; i++) {
        int px = VF_X + 5 + i * 33;
        int py = ground_y - peaks[i];
        int r = (int)(40 * brightness); if (r > 255) r = 255;
        int g = (int)(55 * brightness); if (g > 255) g = 255;
        int b = (int)(45 * brightness); if (b > 255) b = 255;
        DrawTriangle(
            (Vector2){px, py},
            (Vector2){px + 40, ground_y},
            (Vector2){px - 10, ground_y},
            (Color){r, g, b, bg_alpha}
        );
    }

    /* Subject (foreground — always in focus) */
    /* Simple figure silhouette */
    int subj_cx = VF_X + VF_W / 2;
    int subj_base = VF_Y + VF_H - 10;
    float sb = brightness;
    int sr = (int)(140 * sb); if (sr > 255) sr = 255;
    int sg = (int)(100 * sb); if (sg > 255) sg = 255;
    int sbi = (int)(80 * sb); if (sbi > 255) sbi = 255;
    Color subj_col = {sr, sg, sbi, 255};
    /* Body */
    DrawRectangle(subj_cx - 15, subj_base - 80, 30, 50, subj_col);
    /* Head */
    DrawCircle(subj_cx, subj_base - 95, 18, subj_col);
    /* Legs */
    DrawRectangle(subj_cx - 14, subj_base - 30, 12, 30, subj_col);
    DrawRectangle(subj_cx + 2, subj_base - 30, 12, 30, subj_col);

    /* ISO noise overlay */
    if (noise_level > 0.0f) {
        vf_noise_seed += 0.1f;
        for (int ny = 0; ny < VF_H; ny += 3) {
            for (int nx = 0; nx < VF_W; nx += 3) {
                float n = sinf(nx * 13.7f + ny * 7.3f + vf_noise_seed * 100.0f) * 0.5f + 0.5f;
                if (n < noise_level) {
                    unsigned char nv = (unsigned char)(n * 180);
                    DrawRectangle(VF_X + nx, VF_Y + ny, 2, 2,
                                  (Color){nv, nv, nv, (unsigned char)(noise_level * 120)});
                }
            }
        }
    }

    /* Exposure meter */
    int meter_y = VF_Y + VF_H - 20;
    DrawRectangle(VF_X + 30, meter_y, VF_W - 60, 2, (Color){80, 80, 80, 200});
    /* Center mark */
    DrawRectangle(VF_X + VF_W/2 - 1, meter_y - 4, 2, 10, COL_TEXT);
    /* -2..+2 marks */
    for (int m = -2; m <= 2; m++) {
        int mx = VF_X + VF_W/2 + m * 40;
        DrawRectangle(mx, meter_y - 2, 1, 6, COL_DIM);
    }
    /* Needle */
    float meter_pos = -(ev - 8.0f) * 20.0f; /* center = correct exposure */
    if (meter_pos < -80) meter_pos = -80;
    if (meter_pos > 80) meter_pos = 80;
    Color needle_col = (fabsf(meter_pos) < 15) ? COL_GREEN : COL_AMBER;
    DrawCircle(VF_X + VF_W/2 + (int)meter_pos, meter_y, 4, needle_col);

    /* Focus brackets */
    int fc = VF_X + VF_W/2;
    int fcy = VF_Y + VF_H/2 - 15;
    DrawRectangleLines(fc - 20, fcy - 15, 40, 30, (Color){200, 50, 50, 180});
}

/* ── Drawing: Control sliders ──────────────────────────────────────────── */
static void draw_controls(void)
{
    int cx = 60;
    int cy = BODY_Y + BODY_H + 30;
    int slider_w = 130;

    /* Aperture */
    DrawText("APERTURE", cx, cy, 10, COL_DIM);
    char apt_label[16];
    snprintf(apt_label, sizeof(apt_label), "f/%.1f", apertures[apt_idx]);
    DrawText(apt_label, cx + slider_w + 10, cy, 10, COL_AMBER);
    DrawRectangle(cx, cy + 16, slider_w, 3, COL_BORDER);
    float apt_pos = (float)apt_idx / (NUM_APERTURES - 1) * slider_w;
    DrawCircle(cx + (int)apt_pos, cy + 17, 6, COL_AMBER);

    /* Shutter */
    cx += 170;
    DrawText("SHUTTER", cx, cy, 10, COL_DIM);
    DrawText(shutter_labels[shtr_idx], cx + slider_w + 10, cy, 10, COL_ACCENT);
    DrawRectangle(cx, cy + 16, slider_w, 3, COL_BORDER);
    float shtr_pos = (float)shtr_idx / (NUM_SHUTTERS - 1) * slider_w;
    DrawCircle(cx + (int)shtr_pos, cy + 17, 6, COL_ACCENT);

    /* ISO */
    cx += 170;
    DrawText("ISO", cx, cy, 10, COL_DIM);
    char iso_label[16];
    snprintf(iso_label, sizeof(iso_label), "%d", iso_vals[iso_idx]);
    DrawText(iso_label, cx + slider_w + 10, cy, 10, COL_GREEN);
    DrawRectangle(cx, cy + 16, slider_w, 3, COL_BORDER);
    float iso_pos = (float)iso_idx / (NUM_ISOS - 1) * slider_w;
    DrawCircle(cx + (int)iso_pos, cy + 17, 6, COL_GREEN);

    /* Instructions */
    DrawText("A / D  or  \xe2\x86\x90 / \xe2\x86\x92  cycle selection      W / S  or  \xe2\x86\x91 / \xe2\x86\x93  adjust value",
             cx - 340 + 50, cy + 40, 10, (Color){70, 70, 90, 255});
}

/* ── Drawing: Info panel ───────────────────────────────────────────────── */
static void draw_info_panel(void)
{
    /* Background */
    DrawRectangleRounded((Rectangle){INFO_X, INFO_Y, INFO_W, INFO_H},
                         0.04f, 6, COL_PANEL);
    DrawRectangleRoundedLinesEx((Rectangle){INFO_X, INFO_Y, INFO_W, INFO_H},
                                0.04f, 6, 1, COL_BORDER);

    if (active_hotspot < 0) {
        DrawText("Click any part of the camera to learn what it does.",
                 INFO_X + 20, INFO_Y + INFO_H/2 - 5, 10, COL_DIM);
        return;
    }

    Hotspot *h = &hotspots[active_hotspot];
    unsigned char a = (unsigned char)(info_alpha * 255);
    Color title_col = {h->highlight.r, h->highlight.g, h->highlight.b, a};
    Color text_col  = {COL_TEXT.r, COL_TEXT.g, COL_TEXT.b, a};
    Color dim_col   = {COL_DIM.r, COL_DIM.g, COL_DIM.b, a};

    DrawText(h->title, INFO_X + 20, INFO_Y + 18, 14, title_col);

    int ty = INFO_Y + 50;
    DrawText(h->line1, INFO_X + 20, ty, 10, text_col); ty += 22;
    DrawText(h->line2, INFO_X + 20, ty, 10, dim_col);  ty += 22;
    DrawText(h->line3, INFO_X + 20, ty, 10, dim_col);

    /* Part label badge */
    int lw = MeasureText(h->label, 10);
    DrawRectangleRounded((Rectangle){INFO_X + INFO_W - lw - 30, INFO_Y + 15, lw + 16, 20},
                         0.4f, 4, (Color){h->highlight.r, h->highlight.g, h->highlight.b, 40});
    DrawText(h->label, INFO_X + INFO_W - lw - 22, INFO_Y + 19, 10, title_col);
}

/* ── Drawing: Hotspot highlights ───────────────────────────────────────── */
static void draw_hotspot_highlights(Vector2 mouse)
{
    int hover = hit_test(mouse);

    for (int i = 0; i < NUM_HOTSPOTS; i++) {
        Hotspot *h = &hotspots[i];
        bool show = (i == active_hotspot || i == hover);
        if (!show) continue;

        unsigned char alpha = (i == active_hotspot) ? 50 : 30;
        Color col = {h->highlight.r, h->highlight.g, h->highlight.b, alpha};

        if (h->is_circle) {
            float cx = h->rect.x + h->rect.width / 2;
            float cy = h->rect.y + h->rect.height / 2;
            float r  = h->rect.width / 2;
            DrawCircle(cx, cy, r, col);
            if (i == active_hotspot) {
                DrawCircleLines(cx, cy, r, h->highlight);
            }
        } else {
            DrawRectangleRec(h->rect, col);
            if (i == active_hotspot) {
                DrawRectangleLinesEx(h->rect, 1, h->highlight);
            }
        }

        /* Label on hover */
        if (i == hover && i != active_hotspot) {
            int lw = MeasureText(h->label, 10);
            int lx = (int)(h->rect.x + h->rect.width/2) - lw/2;
            int ly = (int)h->rect.y - 16;
            DrawRectangle(lx - 4, ly - 2, lw + 8, 14, (Color){0,0,0,200});
            DrawText(h->label, lx, ly, 10, h->highlight);
        }
    }
}

/* ── Slider interaction state ──────────────────────────────────────────── */
static int selected_control = 0;  /* 0=aperture, 1=shutter, 2=iso */
static float ctrl_blink = 0;

/* ── Main update/draw ──────────────────────────────────────────────────── */
static void update_and_draw(void)
{
    float dt = GetFrameTime();

    /* Input: mouse clicks on camera parts */
    Vector2 mouse = GetMousePosition();

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int hit = hit_test(mouse);
        if (hit >= 0) {
            active_hotspot = hit;
            info_alpha = 0.0f;
        }

        /* Slider clicks */
        int slider_y = BODY_Y + BODY_H + 46;
        if (mouse.y >= slider_y - 8 && mouse.y <= slider_y + 8) {
            if (mouse.x >= 60 && mouse.x <= 190) {
                apt_idx = (int)((mouse.x - 60) / 130.0f * (NUM_APERTURES - 1) + 0.5f);
                if (apt_idx < 0) apt_idx = 0;
                if (apt_idx >= NUM_APERTURES) apt_idx = NUM_APERTURES - 1;
                selected_control = 0;
            } else if (mouse.x >= 230 && mouse.x <= 360) {
                shtr_idx = (int)((mouse.x - 230) / 130.0f * (NUM_SHUTTERS - 1) + 0.5f);
                if (shtr_idx < 0) shtr_idx = 0;
                if (shtr_idx >= NUM_SHUTTERS) shtr_idx = NUM_SHUTTERS - 1;
                selected_control = 1;
            } else if (mouse.x >= 400 && mouse.x <= 530) {
                iso_idx = (int)((mouse.x - 400) / 130.0f * (NUM_ISOS - 1) + 0.5f);
                if (iso_idx < 0) iso_idx = 0;
                if (iso_idx >= NUM_ISOS) iso_idx = NUM_ISOS - 1;
                selected_control = 2;
            }
        }
    }

    /* Keyboard: cycle control selection */
    if (IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT)) {
        selected_control = (selected_control + 2) % 3;
    }
    if (IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT)) {
        selected_control = (selected_control + 1) % 3;
    }

    /* Keyboard: adjust selected control */
    if (IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP)) {
        if (selected_control == 0 && apt_idx > 0) apt_idx--;
        if (selected_control == 1 && shtr_idx > 0) shtr_idx--;
        if (selected_control == 2 && iso_idx > 0) iso_idx--;
    }
    if (IsKeyPressed(KEY_S) || IsKeyPressed(KEY_DOWN)) {
        if (selected_control == 0 && apt_idx < NUM_APERTURES-1) apt_idx++;
        if (selected_control == 1 && shtr_idx < NUM_SHUTTERS-1) shtr_idx++;
        if (selected_control == 2 && iso_idx < NUM_ISOS-1) iso_idx++;
    }

    /* Animate info panel */
    if (active_hotspot >= 0 && info_alpha < 1.0f) {
        info_alpha += dt * 4.0f;
        if (info_alpha > 1.0f) info_alpha = 1.0f;
    }

    ctrl_blink += dt;

    /* ── Draw ── */
    BeginDrawing();
    ClearBackground(COL_BG);

    /* Title */
    DrawText("DSLR CAMERA SIMULATOR", 60, 20, 14, COL_DIM);
    DrawText("Interactive Explainer", 60, 40, 10, (Color){60,60,80,255});

    /* Instruction hint top-right */
    DrawText("Click camera parts to explore", SCREEN_W - 220, 25, 10, COL_DIM);
    DrawText("Use sliders or keys to adjust settings", SCREEN_W - 270, 40, 10,
             (Color){60,60,80,255});

    /* Camera */
    draw_camera();

    /* Hotspot highlights */
    draw_hotspot_highlights(mouse);

    /* Viewfinder */
    draw_viewfinder();

    /* Info panel */
    draw_info_panel();

    /* Controls */
    draw_controls();

    /* Selected control indicator */
    int ctrl_positions[] = {125, 295, 465};
    int ind_y = BODY_Y + BODY_H + 26;
    float blink_a = (sinf(ctrl_blink * 4.0f) * 0.5f + 0.5f) * 0.4f + 0.6f;
    Color ind_colors[] = {COL_AMBER, COL_ACCENT, COL_GREEN};
    Color ic = ind_colors[selected_control];
    DrawTriangle(
        (Vector2){ctrl_positions[selected_control] - 4, ind_y},
        (Vector2){ctrl_positions[selected_control] + 4, ind_y},
        (Vector2){ctrl_positions[selected_control], ind_y - 6},
        (Color){ic.r, ic.g, ic.b, (unsigned char)(blink_a * 255)}
    );

    EndDrawing();
}

/* ── Entry point ───────────────────────────────────────────────────────── */
int main(void)
{
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(SCREEN_W, SCREEN_H, "DSLR Camera Simulator");

    init_hotspots();

#ifdef PLATFORM_WEB
    emscripten_set_main_loop(update_and_draw, 0, 1);
#else
    SetTargetFPS(60);
    while (!WindowShouldClose()) {
        update_and_draw();
    }
#endif

    CloseWindow();
    return 0;
}
