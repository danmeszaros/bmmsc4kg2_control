extern "C" {
#include <hardware/uart.h>
#include <lwip/apps/mdns.h>
#include <lwip/ip.h>
#include <pico/stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pico/binary_info.h"

#include "dhcpserver/dhcpserver.h"
#include "usb_network.h"
#include "lwip/apps/http_client.h" 
#include "pico/time.h"

#include "lwip/tcp.h"

// lcd headers
#include "DEV_Config.h"
#include "LCD_1in14.h"
#include "GUI_Paint.h"
#include "fonts.h"

}
/*

notes:
  PUT /lens/focus/doAutoFocus
  PUT /lens/iris            
  PUT /video/whiteBalance
  PUT /video/gain

*/

/* serial ======================================= */
#define UART_ID uart0           // Use UART1
#define BAUD_RATE 115200

#define UART_TX_PIN 0           // UART1 TX on GPIO8
#define UART_RX_PIN 1           // UART1 RX on GPIO9 (optional)

void serial_init() {
    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);  // Optional if only TX

    // Optional: enable UART FIFO
    uart_set_fifo_enabled(UART_ID, true);
}

void serial_log(const char* msg) {
    uart_puts(UART_ID, msg);
    uart_puts(UART_ID, "\r\n");
}

/* network ====================================== */

// usb network addresses
static const ip4_addr_t ownip = IPADDR4_INIT_BYTES(10, 0, 7, 5);
//static const ip4_addr_t ownip = IPADDR4_INIT_BYTES(192, 168, 7, 1);
static const ip4_addr_t netmask = IPADDR4_INIT_BYTES(255, 255, 255, 0);
static const ip4_addr_t gateway = IPADDR4_INIT_BYTES(0, 0, 0, 0);

const uint LED_PIN = 25;
const int PORT = 80;
// const int PORT = 4000;

#define BUF_SIZE 2048
#define UNDEF -10000

/** json parser ================================ */
int get_json_value(const char* json, const char* key) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);

    char* pos = strstr(json, pattern);
    if (pos) {
        int value;
        if (sscanf(pos + strlen(pattern), "%d", &value) == 1) {
            return value;
        }
    }

    // Return something invalid if not found or parsing fails
    return UNDEF;
}


/** buttons ==================================== */


class Buttons {
public:

    const static int NUM_BUTTONS = 2;
    static constexpr int BUTTON_PINS[NUM_BUTTONS] = {15, 17};
    inline static volatile uint64_t last_interrupt_time[NUM_BUTTONS] = {0, 0};
    inline static volatile bool button_pressed[NUM_BUTTONS] = {false, false};
    const static int DEBOUNCE_TIME_MS = 200;

    static void gpio_callback(uint gpio, uint32_t events) {
        uint64_t now = time_us_64() / 1000;

        for (int i = 0; i < NUM_BUTTONS; i++) {
            if (gpio == BUTTON_PINS[i]) {
                if ((now - last_interrupt_time[i]) < DEBOUNCE_TIME_MS) {
                    return;  // Ignore bouncing
                }
                last_interrupt_time[i] = now;
    
                if (events & GPIO_IRQ_EDGE_FALL) {
                    button_pressed[i] = true;
                    // printf("Button %d pressed\n", i);
                } else if (events & GPIO_IRQ_EDGE_RISE) {
                    // printf("Button %d released\n", i);
                    }
            }
        }
    }

    Buttons() {
        for (int i = 0; i < NUM_BUTTONS; i++) {
            gpio_init(BUTTON_PINS[i]);
            gpio_set_dir(BUTTON_PINS[i], GPIO_IN);
            gpio_pull_up(BUTTON_PINS[i]);
            gpio_set_irq_enabled(BUTTON_PINS[i],
            GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
        }

        // Set up shared callback
        gpio_set_irq_callback(&gpio_callback);
        irq_set_enabled(IO_IRQ_BANK0, true);
    }

    bool pressed(int i) {
        if (button_pressed[i] == true) {
            button_pressed[i] = false;
            return true;
        }

        return false;
    }
};

/** http ======================================== */

class HttpClient {
public:
    inline static char buff[BUF_SIZE] = {0};
    inline static int recv_len = 0;
    inline static volatile bool reqDone = false;
    inline static char* body = 0;

    // template for PUT method. arg1 is method name (string), second is content len, and 3rd content (string)
    inline static const char headerPut[] = 
        "PUT /control/api/v1/%s HTTP/1.1\r\n"
        "Host: Micro-Studio-Camera-4K-G2.local\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s\r\n";

    // template for GET method. arg 1 is string with method name (e.g. video/gain)
    inline static char headerGet[] =
        "GET /control/api/v1/%s HTTP/1.1\r\n"
        "Host: Micro-Studio-Camera-4K-G2.local\r\n"
        "Accept: application/json\r\n"
        "Connection: close\r\n"
        "\r\n";

    inline static char httpReq[2048] = {0};

    static char* strcasestr(const char* haystack, const char* needle) {
        if (!*needle)
            return (char*)haystack;

        for (; *haystack; ++haystack) {
            const char* h = haystack;
            const char* n = needle;

            while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
                ++h;
                ++n;
            }
    
            if (!*n)
                return (char*)haystack;
        }

        return NULL;
    }


    static int parse_content_length(const char *headers) {
        const char *cl_key = "Content-Length:";
        const char *p = strcasestr(headers, cl_key);  // case-insensitive search, POSIX GNU extension

        if (!p) return -1;  // Content-Length not found

        p += strlen(cl_key);

        // Skip whitespace after "Content-Length:"
        while (*p == ' ' || *p == '\t') p++;

        // Parse number
        int content_length = atoi(p);
        if (content_length < 0) return -1;

        return content_length;
    }

    static err_t recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
        printf("*** recv\n");
        if (pcb != NULL && p != NULL) {
            // Make sure not to overflow your buffer
            int copy_len = p->tot_len;
            if (recv_len + copy_len >= BUF_SIZE) {
                copy_len = BUF_SIZE - recv_len - 1;  // leave space for null
            }
            pbuf_copy_partial(p, buff + recv_len, copy_len, 0);
            recv_len += copy_len;
            buff[recv_len] = 0;

            printf("Received %d bytes, total %d\n", copy_len, recv_len);
            printf("Buffer so far: %s\n", buff);

            tcp_recved(pcb, p->tot_len);
            pbuf_free(p);

            // Do NOT close connection yet, wait for all data
            // Usually connection closes when remote side sends FIN (p == NULL)

            char *header_end = strstr(buff, "\r\n\r\n");
            if (header_end) {
                if (body == 0) {
                    body = header_end + 4;
                }
                // Parse headers here, extract Content-Length value
                int content_length = parse_content_length(buff);

                int header_len = (header_end - buff) + 4;
                int body_len = recv_len - header_len;

                if (body_len >= content_length) {
                    printf("setting reqDone to true (recv1)\n");
                    reqDone = true;  // entire response received
                    tcp_close(pcb);
                    pcb = NULL;
                }
            }

        } else {
            // p == NULL means connection closed by remote side — finish up
            printf("Connection closed by remote, total received: %d bytes\n", recv_len);
            if (pcb != NULL) {
                // reqDone = true;
                // printf("setting reqDone to true (recv2)\n");
            }
            tcp_close(pcb);
            pcb = NULL;
        }
        return ERR_OK;
    }

    static err_t connected(void *arg, struct tcp_pcb *pcb, 
                                          err_t err)
    {
        gpio_put(LED_PIN, 1);  

        printf("*** Connected. Sending header: \n%s", httpReq);
        printf("====================\n");

        err = tcp_write(pcb, httpReq, strlen(httpReq), 0);
        if (err != ERR_OK) {
            tcp_close(pcb);
            pcb = NULL;
            return err;
        }

        err = tcp_output(pcb);
        if (err != ERR_OK) {
            tcp_close(pcb);
            pcb = NULL;
        }
        gpio_put(LED_PIN, 0);
        return ERR_OK;
    }

    static int sendReq() {
        printf("*** sendReq\n");
        recv_len = 0;
        body = 0;
        printf("setting reqDone to false\n");
        reqDone = false;
        struct tcp_pcb *pcb = tcp_new();
        tcp_recv(pcb, recv);
        ip_addr_t ip;
        IP4_ADDR(&ip, 10, 0, 7, 16);

        err_t err = tcp_connect(pcb, &ip, PORT, connected);

        if (err != ERR_OK) {
            tcp_abort(pcb);
        }

        return 0;
    }

    static void sendPutInt(const char* method, const char* property, int value) {
        char buff[128];
        snprintf(buff, sizeof(buff), "{\"%s\": %d}", property, value);
        snprintf(httpReq, sizeof(httpReq), headerPut, method, strlen(buff), buff);
        sendReq();
    }

    static void sendGet(const char* method) {
        char buff[128];
        snprintf(httpReq, sizeof(httpReq), headerGet, method);
        sendReq();
    }
};


/* LCD ========================================== */

class  LCD {
public:

    UWORD *image;

    LCD() {
        DEV_Delay_ms(100);
        DEV_Module_Init();
        LCD_1IN14_Init(HORIZONTAL);
        LCD_1IN14_Clear(WHITE);

        // Create image buffer
        UDOUBLE image_size = LCD_1IN14_HEIGHT * LCD_1IN14_WIDTH * 2;
        image = (UWORD *)malloc(image_size);
        Paint_NewImage((UBYTE *)image, LCD_1IN14.WIDTH, LCD_1IN14.HEIGHT, 0, WHITE);
        Paint_SetScale(65);
        Paint_SetRotate(ROTATE_0);
    }

    void clear() {
        Paint_Clear(WHITE); 
    }

    void write(const char* text, int x, int y) {

        // Draw text — directly pass the font
        Paint_DrawString_EN(x, y, text, &Font16, WHITE, BLACK);

    }

    void flush() {
        // Update display
        LCD_1IN14_Display(image);
        
    }
};

/* app state ==================================== */

class App {
public:
    int gain;
    int wb;
    int shutter;

    int cursor;

    enum Action {
        NONE = 0,
        SET_GAIN = 1,
        SET_WB = 2,
        SET_SHUTTER = 3,

        GET_GAIN = 100,
        GET_WB = 101,
        GET_SHUTTER = 102
    };

    enum ChangeAction {
        UP = 0,
        DOWN = 1
    };

    Action reqAction;

    App() {
        reqAction = NONE;

        gain = 0;
        wb = 3000;
        shutter = 180;

        cursor = 0;
    }

    void updateLCD(LCD& lcd) {
        char buff[16];

        lcd.clear();
/*
        snprintf(buff, sizeof(buff), "%d", wb);
        lcd.write(buff, 10, 20);

        snprintf(buff, sizeof(buff), "%d", gain);
        lcd.write(buff, 40, 40);

        LCD_1IN14_Display(lcd.image);        
*/

        if (HttpClient::body) {
            lcd.write(HttpClient::body, 10, 20);
        } else {
            lcd.write("NULL", 10, 20);
        }

        lcd.flush();
    }

    void changeGain(ChangeAction action) {
        int step = 6;

        if (action == UP && gain + step <= 32) {
            gain += 6;
        } else if (action == DOWN && gain - step >= -12) {
            gain -= 6;
        } else {
            return;
        }

        reqAction = SET_GAIN;
        HttpClient::sendPutInt("video/gain", "gain", gain);

    }

    bool updateState() {
        if (HttpClient::reqDone == false) {
            // request not done yet
            return false;
        }

        if (reqAction == SET_GAIN) {
            printf("in SET_GAIN\n");
            HttpClient::sendGet("video/gain");
            reqAction = GET_GAIN;
            printf("next action: GET_GAIN\n");
            return false;
        }


        if (reqAction == GET_GAIN) {
            printf("in GET_GAIN\n");
            reqAction = NONE;

            printf("body received:\n====%s====\n", HttpClient::body);
            

            int newGain = get_json_value(HttpClient::body, "gain");
            printf("parsed value: %d\n", newGain);
            if (newGain != UNDEF) {
                gain = newGain;

                return true;
            } else {
                /*
                gain = -666;
                return true;
                */
            }
        }

        return false;
    }

};

int main() {
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    // show we're alive
    gpio_put(LED_PIN, 1);
    sleep_ms(500);
    gpio_put(LED_PIN, 0);

    stdio_uart_init();

    serial_init();
    serial_log("Serial initialized");

    Buttons buttons;
    HttpClient httpClient;
    App app;

    LCD lcd;
    lcd.write("initializing...", 10, 20);

    // setup USB network
    if (!usb_network_init(&ownip, &netmask, &gateway, true)) {
        printf("failed to start usb network\n");
        return -1;
    }

    // setup DHCP server
    dhcp_server_t dhcp_server;
    dhcp_server_init(&dhcp_server, (ip_addr_t *)&ownip, (ip_addr_t *)&netmask, false);

    // enable mDNS
    mdns_resp_init();
    mdns_resp_add_netif(netif_default, "demo");

    // enter main loop
    printf("setup complete, entering main loop\n");
    int key = 0;

    int lastState = 0;
    int alarm = 5;

    app.updateLCD(lcd);


    while ((key != 's') && (key != 'S')) {
        usb_network_update();
        key = getchar_timeout_us(0); // get any pending key press but don't wait

        uint64_t microseconds = time_us_64();
        uint64_t seconds = microseconds / 1000000;
        uint64_t state = seconds % 2;

/*
        if (seconds > alarm) {
            sendReq();
            gpio_put(LED_PIN, 0);
            sleep_ms(100);

            alarm = seconds + 5; 
        }
*/

        const char* fn = 0;

        //int newGain = gain;
        if (buttons.pressed(0)) {
            app.changeGain(App::DOWN);
        }

        if (buttons.pressed(1)) {
            app.changeGain(App::UP);
        }

        if (app.updateState()) {
            app.updateLCD(lcd);
        }

#if 0
/*    
        if (HttpClient::reqDone) {
            HttpClient::buff[300] = 0;

            if (HttpClient::body) {
                lcd.write(HttpClient::body, 10, 20);
            }
*/

/*
            char *pos = strstr(myBuff, "{");
            //myBuff[100] = 0;
            if (pos != NULL) {
                pos[100] = 0;
                lcd.write(pos);
            }
*/
            HttpClient::reqDone = false;
        }
#endif

        // prevent cpu burning (?)
        tight_loop_contents();

    }

    printf("shutting down\n");
    mdns_resp_remove_netif(netif_default);
    dhcp_server_deinit(&dhcp_server);
    usb_network_deinit();

    return 0;
}
