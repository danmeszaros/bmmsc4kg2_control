// #define DBG


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
}

#include <set>
#include <string>
#include <vector>

#include "button.h"

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

/* network ====================================== */

#ifndef DBG

#define BUTTON_FOCUS 10
#define BUTTON_AUX 13
#define BUTTON_RECORD 14

#else
// define dbg buttons
#endif

// usb network addresses
static const ip4_addr_t ownip = IPADDR4_INIT_BYTES(10, 0, 7, 5);
//static const ip4_addr_t ownip = IPADDR4_INIT_BYTES(192, 168, 7, 1);
static const ip4_addr_t netmask = IPADDR4_INIT_BYTES(255, 255, 255, 0);
static const ip4_addr_t gateway = IPADDR4_INIT_BYTES(0, 0, 0, 0);

// const uint LED_PIN = 25;
const int PORT = 80;
// const int PORT = 4000;

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

#define UNDEF_BOOL -1  // or use a separate error flag

int get_json_bool(const char* json, const char* key) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);

    char* pos = strstr(json, pattern);
    if (pos) {
        pos += strlen(pattern);
        // Skip whitespace after colon
        while (*pos == ' ' || *pos == '\t' || *pos == '\n') {
            pos++;
        }

        if (strncmp(pos, "true", 4) == 0) {
            return 1;
        } else if (strncmp(pos, "false", 5) == 0) {
            return 0;
        }
    }

    return UNDEF_BOOL;
}

/** http ======================================== */

class HttpRequest {
public:

    int id;
    bool done;
    uint64_t startTs;

    std::string requestString;

    int contentLength;

    std::string responseString;
    size_t headerEndPos; // pointing behing /r/n/r/n
    int action;

    HttpRequest(int _id, int _action) {
        id = _id;
        done = false;
        headerEndPos = std::string::npos;
        startTs = time_us_64();
        action = _action;
    }
};

class HttpClient {
public:
    std::set<HttpRequest*> activeRequests;
    std::vector<HttpRequest*> doneRequests;
    int cnter = 0;

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

    static int sendReq(HttpRequest* req) {
        printf("*** #%d sendReq2\n", req->id);

        struct tcp_pcb *pcb = tcp_new();
        tcp_arg(pcb, req);
        tcp_recv(pcb, HttpClient::recv);
        ip_addr_t ip;
        IP4_ADDR(&ip, 10, 0, 7, 16);

        err_t err = tcp_connect(pcb, &ip, PORT, HttpClient::connected);

        if (err != ERR_OK) {
            tcp_abort(pcb);
        }

        return 0;
    }

    static err_t connected(void *arg, struct tcp_pcb *pcb, err_t err) {
        HttpRequest *req = (HttpRequest*)arg;

        printf("*** #%d Connected. Sending header: \n%s", req->id, req->requestString.c_str());
        printf("====================\n");

        err = tcp_write(pcb, req->requestString.c_str(), req->requestString.size(), 0);
        if (err != ERR_OK) {
            tcp_close(pcb);
            pcb = NULL;
            req->done = true;
            return err;
        }

        err = tcp_output(pcb);
        if (err != ERR_OK) {
            tcp_close(pcb);
            req->done = true;
            pcb = NULL;
        }
        // gpio_put(LED_PIN, 0);
        return ERR_OK;
    }

    static err_t recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
        HttpRequest *req = (HttpRequest*)arg;

        printf("*** #%d recv\n", req->id);

        if (!p) {
            // Remote side closed the connection
            tcp_close(pcb);
            req->done = true;
            return ERR_OK;
        }

        if (err != ERR_OK) {
            // Some error occurred, free buffer and bail
            pbuf_free(p);
            req->done = true;
            return err;
        }

        struct pbuf *q = p;
        while (q != nullptr) {
            req->responseString.append(static_cast<char*>(q->payload), q->len);
            q = q->next;
        }

        // Tell lwIP we have received the data
        tcp_recved(pcb, p->tot_len);

        printf("========= received\n%s\n================\n", req->responseString.c_str());

        // Free the pbuf
        pbuf_free(p);

        // check if we are finished. this means that we have headers and body matches
        // content-length
        char *header_end = strstr(req->responseString.c_str(), "\r\n\r\n");
        
        if (header_end) {
            printf("header end found\n");
            req->headerEndPos = (header_end - req->responseString.c_str()) + 4;

            // Parse headers here, extract Content-Length value
                int content_length = parse_content_length(req->responseString.c_str());
                printf("content length: %d\n", content_length);

                // int header_len = (header_end - buff) + 4;
                int body_len = req->responseString.size() - req->headerEndPos;
                printf("bodylen = %d\n", body_len);

                if (body_len >= content_length) {
                    printf("setting reqDone to true (recv1)\n");
                    tcp_close(pcb);
                    req->done = true;
                    pcb = NULL;
                }
        } else {
            printf("header end not found yet\n");
        }

        return ERR_OK;
    }

    bool newPutRequest(int action,
            const std::string& path, const std::string& body) {
        HttpRequest* req = new HttpRequest(cnter++, action);

        // fill req headers
        char buff[512];
        snprintf(buff, sizeof(buff),
            "PUT /control/api/v1/%s HTTP/1.1\r\n"
            "Host: Micro-Studio-Camera-4K-G2.local\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n"
            "%s\r\n",
            path.c_str(), body.size(), body.c_str());
            
        req->requestString.assign(buff);
        activeRequests.insert(req);

        // send request to server
        sendReq(req);

        return true;
    }

    bool newGetRequest(int action, const std::string& path) {
        HttpRequest* req = new HttpRequest(cnter++, action);

        char buff[256];
        snprintf(buff, sizeof(buff),
            //"GET /control/api/v1/%s HTTP/1.1\r\n"
              "GET /control/%s HTTP/1.1\r\n"
            "Host: Micro-Studio-Camera-4K-G2.local\r\n"
            "Accept: application/json\r\n"
            "Connection: close\r\n"
            "\r\n",
            path.c_str());

        req->requestString.assign(buff);
        activeRequests.insert(req);

        sendReq(req);

        return true;
    }

    void updateQueue() {
        bool restart;
        do { // simple way to restart loop
            restart = false;
            auto it = activeRequests.begin();
            while (it != activeRequests.end()) {
                if ((*it)->done == true) {
                    printf("request %d done\n", (*it)->id);
                    doneRequests.push_back(*it);
                    activeRequests.erase(it);
                    restart = true;
                    break;
                }
                it++;
            }
        } while (restart);
    }
};

/* app state ==================================== */

class App {
public:
    enum ActionType {
        DO_RECORD,
        DO_STOP,
        DO_FOCUS,
        SET_CLEANFEED,
        SET_APERTURE,
        GET_APERTURE,
        SET_GAIN,
        GET_GAIN,
        SET_WB,
        GET_WB,
        SET_DEBUG
    } actionType;

    HttpClient httpClient;

    int gain;
    int wbIndex;
    int wb;
    int shutter;

    int cursor;

    int record;
    int cleanFeed;

    std::vector<int> wbValues = {2800, 4000, 5200, 6000, 7000};

    enum ChangeAction {
        UP = 0,
        DOWN = 1
    };

    App() {

        gain = 0;
        wb = 3000;
        wbIndex = 0;
        shutter = 180;

        cursor = 0;

        record = 0;
        cleanFeed = 0;
    }

    void sendDebugRequest(const std::string& message) {
        httpClient.newPutRequest(SET_DEBUG, "debug/" + message, "");
    }

    void changeGain(ChangeAction action) {
        int step = 6;

        if (action == UP && gain + step <= 36) {
            gain += step;
        } else if (action == DOWN && gain - step >= -12) {
            gain -= step;
        } else {
            return;
        }

        // HttpClient::sendPutInt("video/gain", "gain", gain);
    }

    void doAutoFocus() {
        httpClient.newPutRequest(DO_FOCUS, "lens/focus/doAutoFocus", "");
    }

    void toggleRecord() {
        record = 1 - record;

        if (record == 1) {
            httpClient.newPutRequest(DO_RECORD, "transports/0/record", "{\"recording\": true}");
        } else {
            httpClient.newPutRequest(DO_STOP, "transports/0/stop", "");
        }
    }

    void toggleNativeGain() {
        if (gain == 0) {
            gain = 18;
        } else {
            gain = 0;
        }

        char arg[32];
        snprintf(arg, sizeof(arg), "{\"gain\": %d}", gain);
    
        httpClient.newPutRequest(SET_GAIN, "video/gain", arg);
    }

    void cycleWB() {
        wbIndex ++;
        if (wbIndex >= wbValues.size()) {
            wbIndex = 0;
        }

        char arg[32];
        snprintf(arg, sizeof(arg), "{\"whiteBalance\": %d}", wbValues[wbIndex]);

        httpClient.newPutRequest(SET_GAIN, "video/whiteBalance", arg);
    }

    void changeWB(ChangeAction action) {
        int step = 100;

        if (action == UP && wb + step <= 9900) {
            wb += step;
        } else if (action == DOWN && wb - step >= 1800) {
            wb -= step;
        } else {
            return;
        }

        // HttpClient::sendPutInt("video/whiteBalance", "whiteBalance", wb);
    }

    void autoWB() {
        httpClient.newPutRequest(SET_GAIN, "video/whiteBalance/doAuto", "");
    }

    bool updateState() {
        return true;
    }

    void changeCursor(int diff) {
        cursor += diff;
        if (cursor < 0) {
            cursor = 0;
        }

        if (cursor > 3) {
            cursor = 3;
        }
    }

};

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


int main() {
    // set ground for buttons
#ifndef DBG
    gpio_init(11);
    gpio_set_dir(11, GPIO_OUT);
    gpio_put(11, 0);

    gpio_init(12);
    gpio_set_dir(12, GPIO_OUT);
    gpio_put(12, 0);

    gpio_init(15);
    gpio_set_dir(15, GPIO_OUT);
    gpio_put(15, 0);
#endif

    stdio_uart_init();

    serial_init();
    printf("Serial initialized\n");

    sleep_ms(500);

    App app;

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

    Button buttonRecord(BUTTON_RECORD);
    Button buttonFocus(BUTTON_FOCUS);
    Button buttonAux(BUTTON_AUX);

    while (true) {
        usb_network_update();

        // RECORD button
        if (buttonRecord.shortPressed()) {
            app.toggleRecord();
        }

        // FOCUS button
        if (buttonFocus.shortPressed()) {
            app.doAutoFocus();
        }

        if (buttonFocus.longPressed()) {
            app.toggleNativeGain();
        }


        // AUX button
        if (buttonAux.shortPressed()) {
            app.cycleWB();
        }

        if (buttonAux.longPressed()) {
            app.autoWB();
        }

#if 0
        if (buttonFocus.pressed()) {
            app.sendDebugRequest("pressed");
        }

        if (buttonFocus.released(wasShort)) {
            if (wasShort) {
                app.sendDebugRequest("releaseShort");
            } else {
                app.sendDebugRequest("releaseLong");
            }
        }

        if (buttonFocus.longPressed()) {
            app.sendDebugRequest("longPress");
        }
#endif

        app.updateState();

        // prevent cpu burning (?)
        tight_loop_contents();

    }

    printf("shutting down\n");
    mdns_resp_remove_netif(netif_default);
    dhcp_server_deinit(&dhcp_server);
    usb_network_deinit();

    return 0;
}
