/*
 * Practica 1 SO — Bootstrapping
 * Secuencia de arranque seguro e integridad de plataforma (ESP32)
 * Marcapasos VVI — POST de Flash/SRAM + Task WDT + estado seguro
 *
 * Como usar en Wokwi (https://wokwi.com/projects/new/esp32):
 *   1. Pegar este archivo como sketch.ino y diagram.json en el proyecto.
 *   2. Dejar TEST_MODE en 0, iniciar la simulacion y copiar la consola.
 *   3. Cambiar TEST_MODE (1..6), reiniciar la simulacion y documentar cada caso.
 */

#include <Arduino.h>
#include <string.h>
#include <esp_system.h>
#include <esp_crc.h>
#include <esp_task_wdt.h>
#include <esp_sleep.h>
#include <esp_idf_version.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/* -------------------------------------------------------------------------- */
/*  Modo de prueba — cambiar UN valor y reiniciar la simulacion               */
/*    0  arranque normal (POST OK, alimenta el WDT)                           */
/*    1  esp_restart()                 -> ESP_RST_SW                          */
/*    2  escritura a NULL              -> ESP_RST_PANIC                       */
/*    3  bucle infinito en loop()      -> ESP_RST_TASK_WDT                    */
/*    4  deep sleep 3 s                -> ESP_RST_DEEPSLEEP                   */
/*    5  forzar fallo CRC de Flash     -> estado seguro, sin bootloop         */
/*    6  forzar fallo March de SRAM    -> estado seguro, sin bootloop         */
/* -------------------------------------------------------------------------- */
#ifndef TEST_MODE
#define TEST_MODE 0
#endif

#define WDT_TIMEOUT_SECONDS 3
#define SRAM_TEST_SIZE      128
#define PATTERN_A           0xAA55AA55u
#define PATTERN_B           0x55AA55AAu

const uint32_t EXPECTED_FLASH_CRC = 0xC3576568;
const uint8_t flash_block_test[64] = {
    0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x55,
    0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x55
};

static volatile uint32_t sram_test_buffer[SRAM_TEST_SIZE];

static void print_reset_reason(void);
static void init_watchdog(void);
static bool post_flash_crc(void);
static bool post_sram_march(void);
static void enter_safe_state(const char *reason);

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println();
    Serial.println("=== Marcapasos VVI :: Bootstrapping / POST ===");

    /* (a) Lectura e impresion de la causa de reinicio */
    print_reset_reason();

    /* (b) Inicializacion del Watchdog de tareas (TWDT) */
    init_watchdog();

    /* (c) POST de Flash (CRC32) */
    if (!post_flash_crc()) {
        enter_safe_state("Integridad de Flash fallida (CRC32 no coincide)");
    }

    /* (d) POST de SRAM (March test con patrones complementarios) */
    if (!post_sram_march()) {
        enter_safe_state("Fallo en celdas SRAM (March test)");
    }

    /* (e) POST OK: alimentar WDT y continuar hacia la logica de aplicacion */
    esp_task_wdt_reset();
    Serial.println("[POST] Autodiagnostico de plataforma CONCLUIDO correctamente.");
    Serial.println("[BOOT] Control transferido a la logica de aplicacion.");
}

void loop() {
#if TEST_MODE == 1
    Serial.println("[TEST] Provocando reinicio por software...");
    Serial.flush();
    esp_restart();
    delay(10);
#elif TEST_MODE == 2
    Serial.println("[TEST] Provocando panic (escritura a NULL)...");
    Serial.flush();
    int *p = NULL;
    *p = 42;
#elif TEST_MODE == 3
    Serial.println("[TEST] Bloqueo deliberado: while(1) sin alimentar WDT...");
    Serial.flush();
    while (1) {
    }
#elif TEST_MODE == 4
    Serial.println("[TEST] Entrando a deep sleep (3 s)...");
    Serial.flush();
    esp_sleep_enable_timer_wakeup(3000000);
    esp_deep_sleep_start();
#else
    /* Operacion nominal: el hilo de aplicacion alimenta el WDT */
    esp_task_wdt_reset();
    delay(500);
#endif
}

static void print_reset_reason(void) {
    const esp_reset_reason_t reason = esp_reset_reason();
    Serial.printf("[RESET] Codigo: %d — ", static_cast<int>(reason));

    switch (reason) {
    case ESP_RST_UNKNOWN:
        Serial.println("Causa desconocida (no se pudo determinar).");
        break;
    case ESP_RST_POWERON:
        Serial.println("Encendido inicial (power-on).");
        break;
    case ESP_RST_EXT:
        Serial.println("Reinicio por pin externo (no aplica en ESP32 clasico).");
        break;
    case ESP_RST_SW:
        Serial.println("Reinicio por software (esp_restart).");
        break;
    case ESP_RST_PANIC:
        Serial.println("Reinicio por excepcion / panic.");
        break;
    case ESP_RST_INT_WDT:
        Serial.println("Reinicio por Interrupt Watchdog (IWDT).");
        break;
    case ESP_RST_TASK_WDT:
        Serial.println("Reinicio por Task Watchdog (TWDT).");
        break;
    case ESP_RST_WDT:
        Serial.println("Reinicio por otro watchdog de hardware.");
        break;
    case ESP_RST_DEEPSLEEP:
        Serial.println("Despertar desde deep sleep.");
        break;
    case ESP_RST_BROWNOUT:
        Serial.println("Fallo de alimentacion (brownout).");
        break;
    case ESP_RST_SDIO:
        Serial.println("Reinicio por SDIO.");
        break;
    default:
        Serial.println("Causa no catalogada en este firmware.");
        break;
    }
}

static void init_watchdog(void) {
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_task_wdt_config_t twdt_config = {
        .timeout_ms = WDT_TIMEOUT_SECONDS * 1000,
        .idle_core_mask = 0, /* idle no alimenta el WDT: un hang se detecta */
        .trigger_panic = true
    };

    esp_err_t err = esp_task_wdt_init(&twdt_config);
    if (err == ESP_ERR_INVALID_STATE) {
        err = esp_task_wdt_reconfigure(&twdt_config);
    }
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        Serial.printf("[WDT] init/reconfigure fallo: %s\n", esp_err_to_name(err));
    }
#else
    /* Arduino-ESP32 2.x / IDF 4.x (tipico en Wokwi web).
     * init() vuelve a suscribir los idle; hay que quitarlos despues
     * para que un hang en loop() dispare el TWDT. */
    esp_task_wdt_init(WDT_TIMEOUT_SECONDS, true);
    disableCore0WDT();
    disableCore1WDT();
#endif

    const esp_err_t add_err = esp_task_wdt_add(NULL);
    if (add_err != ESP_OK && add_err != ESP_ERR_INVALID_ARG &&
        add_err != ESP_ERR_INVALID_STATE) {
        Serial.printf("[WDT] add fallo: %s\n", esp_err_to_name(add_err));
    }

    Serial.printf("[WDT] Task WDT activo, timeout=%d s, panic=ON\n",
                  WDT_TIMEOUT_SECONDS);
}

static bool post_flash_crc(void) {
    const uint8_t *block = flash_block_test;
    uint8_t corrupted[sizeof(flash_block_test)];

#if TEST_MODE == 5
    memcpy(corrupted, flash_block_test, sizeof(flash_block_test));
    corrupted[0] ^= 0xFFu;
    block = corrupted;
    Serial.println("[POST][Flash] Modo prueba: un byte del bloque fue alterado.");
#endif

    const uint32_t crc = esp_crc32_le(0, block, sizeof(flash_block_test));
    Serial.printf("[POST][Flash] CRC32 calculado=0x%08X  esperado=0x%08X\n",
                  crc, EXPECTED_FLASH_CRC);

    if (crc != EXPECTED_FLASH_CRC) {
        Serial.println("[POST][Flash] FALLO: imagen/bloque inconsistente.");
        return false;
    }

    Serial.println("[POST][Flash] OK");
    return true;
}

/*
 * March-lite: cada celda se escribe/lee con A, luego con B (complemento),
 * y se deja en 0. A y B ejercitan TODOS los bits en 0 y en 1, de modo que
 * un stuck-at-0 o stuck-at-1 no pueda pasar desapercibido.
 */
static bool post_sram_march(void) {
#if TEST_MODE == 6
    Serial.println("[POST][SRAM] Modo prueba: se inyecta un fallo de retencion.");
#endif

    for (size_t i = 0; i < SRAM_TEST_SIZE; ++i) {
        sram_test_buffer[i] = PATTERN_A;
        if (sram_test_buffer[i] != PATTERN_A) {
            Serial.printf("[POST][SRAM] FALLO patron A en indice %u\n",
                          static_cast<unsigned>(i));
            return false;
        }

        sram_test_buffer[i] = PATTERN_B;
        if (sram_test_buffer[i] != PATTERN_B) {
            Serial.printf("[POST][SRAM] FALLO patron B en indice %u\n",
                          static_cast<unsigned>(i));
            return false;
        }

        sram_test_buffer[i] = 0x00000000u;
        if (sram_test_buffer[i] != 0x00000000u) {
            Serial.printf("[POST][SRAM] FALLO al restablecer indice %u\n",
                          static_cast<unsigned>(i));
            return false;
        }

#if TEST_MODE == 6
        if (i == 0) {
            Serial.println("[POST][SRAM] FALLO: celda 0 no retuvo el patron (inyectado).");
            return false;
        }
#endif
    }

    Serial.printf("[POST][SRAM] OK (%u palabras, patrones A/B)\n",
                  static_cast<unsigned>(SRAM_TEST_SIZE));
    return true;
}

static void enter_safe_state(const char *reason) {
    Serial.printf("[CRITICAL ERROR] %s\n", reason);
    Serial.println("[CRITICAL ERROR] Estimulacion INHIBIDA. Estado seguro retenido.");
    Serial.println("[CRITICAL ERROR] WDT desuscrito para evitar bootloop.");
    Serial.flush();

    /* Evita reinicios continuos: este hilo deja de ser vigilado. */
    (void)esp_task_wdt_delete(NULL);

    while (1) {
        /* CPU detenida en fallo irrecuperable. No hay pacing ni salidas. */
    }
}
