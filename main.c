// Servidor FTP simples no PS3: envie músicas para /dev_hdd0/music
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ppu-types.h>
#include <sysutil/sysutil.h>
#include <sysmodule/sysmodule.h>
#include <net/net.h>

#define MUSIC_DIR  "/dev_hdd0/music"
#define CTRL_PORT  21
#define DATA_PORT  50000          // porta passiva: 50000 = 195*256 + 80
#define FTP_USER   "ps3"
#define FTP_PASS   "ps3"          // troque por uma senha sua

static volatile int g_running = 1;

static void sysutil_callback(u64 status, u64 param, void *userdata)
{
    (void)param;
    (void)userdata;
    if (status == SYSUTIL_EXIT_GAME)
        g_running = 0;
}

static int make_listen_socket(uint16_t port)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        return -1;

    int opt = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0 || listen(s, 2) < 0) {
        close(s);
        return -1;
    }
    return s;
}

static void send_str(int s, const char *msg)
{
    send(s, msg, strlen(msg), 0);
}

// Lê uma linha terminada em \n (ignora \r). Retorna -1 se a conexão caiu.
static int recv_line(int s, char *buf, int max)
{
    int i = 0;
    char c;
    while (i < max - 1) {
        int r = recv(s, &c, 1, 0);
        if (r <= 0)
            return -1;
        if (c == '\n')
            break;
        if (c != '\r')
            buf[i++] = c;
    }
    buf[i] = '\0';
    return i;
}

static int safe_name(const char *name)
{
    if (!name[0])
        return 0;
    if (strchr(name, '/') || strstr(name, ".."))
        return 0;
    return 1;
}

static void make_path(char *out, size_t size, const char *name)
{
    snprintf(out, size, "%s/%s", MUSIC_DIR, name);
}

static int accept_data(int data_listen)
{
    return accept(data_listen, NULL, NULL);
}

static void cmd_list(int ctrl, int data_listen)
{
    send_str(ctrl, "150 Listando\r\n");
    int d = accept_data(data_listen);
    if (d < 0) {
        send_str(ctrl, "425 Sem conexao de dados\r\n");
        return;
    }

    DIR *dir = opendir(MUSIC_DIR);
    if (dir) {
        struct dirent *de;
        char path[512], line[600];
        while ((de = readdir(dir)) != NULL) {
            if (de->d_name[0] == '.')
                continue;
            make_path(path, sizeof(path), de->d_name);
            struct stat st;
            long size = (stat(path, &st) == 0) ? (long)st.st_size : 0;
            snprintf(line, sizeof(line),
                     "-rw-rw-rw- 1 ps3 ps3 %ld Jan 01 2000 %s\r\n", size, de->d_name);
            send(d, line, strlen(line), 0);
        }
        closedir(dir);
    }

    close(d);
    send_str(ctrl, "226 Lista enviada\r\n");
}

static void cmd_stor(int ctrl, int data_listen, const char *name)
{
    if (!safe_name(name)) {
        send_str(ctrl, "553 Nome invalido\r\n");
        return;
    }

    char path[512];
    make_path(path, sizeof(path), name);

    FILE *f = fopen(path, "wb");
    if (!f) {
        send_str(ctrl, "553 Nao foi possivel criar o arquivo\r\n");
        return;
    }

    send_str(ctrl, "150 Pronto para receber\r\n");
    int d = accept_data(data_listen);
    if (d < 0) {
        fclose(f);
        send_str(ctrl, "425 Sem conexao de dados\r\n");
        return;
    }

    char buf[4096];
    int n;
    while ((n = recv(d, buf, sizeof(buf), 0)) > 0)
        fwrite(buf, 1, n, f);

    fclose(f);
    close(d);
    send_str(ctrl, "226 Arquivo salvo\r\n");
}

static void cmd_dele(int ctrl, const char *name)
{
    if (!safe_name(name)) {
        send_str(ctrl, "553 Nome invalido\r\n");
        return;
    }
    char path[512];
    make_path(path, sizeof(path), name);
    if (remove(path) == 0)
        send_str(ctrl, "250 Arquivo apagado\r\n");
    else
        send_str(ctrl, "550 Arquivo nao encontrado\r\n");
}

static void handle_session(int ctrl, int data_listen)
{
    char line[512], cmd[16], arg[400];
    char user[64] = "";
    int logged_in = 0;
    int pasv_ready = 0;

    send_str(ctrl, "220 PS3 Music FTP pronto\r\n");

    while (g_running) {
        if (recv_line(ctrl, line, sizeof(line)) < 0)
            break;

        cmd[0] = '\0';
        arg[0] = '\0';
        sscanf(line, "%15s %399[^\n]", cmd, arg);

        for (char *p = cmd; *p; p++)
            if (*p >= 'a' && *p <= 'z')
                *p -= 32;

        if (!strcmp(cmd, "USER")) {
            strncpy(user, arg, sizeof(user) - 1);
            send_str(ctrl, "331 Envie a senha\r\n");
            continue;
        }
        if (!strcmp(cmd, "PASS")) {
            if (!strcmp(user, FTP_USER) && !strcmp(arg, FTP_PASS)) {
                logged_in = 1;
                send_str(ctrl, "230 Logado\r\n");
            } else {
                send_str(ctrl, "530 Usuario ou senha invalidos\r\n");
            }
            continue;
        }
        if (!strcmp(cmd, "QUIT")) {
            send_str(ctrl, "221 Tchau\r\n");
            break;
        }
        if (!strcmp(cmd, "NOOP")) {
            send_str(ctrl, "200 OK\r\n");
            continue;
        }
        if (!logged_in) {
            send_str(ctrl, "530 Faca login primeiro\r\n");
            continue;
        }

        if (!strcmp(cmd, "SYST")) {
            send_str(ctrl, "215 UNIX Type: L8\r\n");
        } else if (!strcmp(cmd, "TYPE")) {
            send_str(ctrl, "200 Tipo ok\r\n");
        } else if (!strcmp(cmd, "PWD") || !strcmp(cmd, "XPWD")) {
            send_str(ctrl, "257 \"/\" e o diretorio atual\r\n");
        } else if (!strcmp(cmd, "CWD") || !strcmp(cmd, "CDUP")) {
            send_str(ctrl, "250 OK\r\n");   // pasta única: musicas
        } else if (!strcmp(cmd, "PASV")) {
            struct sockaddr_in me;
            socklen_t len = sizeof(me);
            getsockname(ctrl, (struct sockaddr *)&me, &len);
            uint8_t *ip = (uint8_t *)&me.sin_addr.s_addr;
            char resp[128];
            snprintf(resp, sizeof(resp),
                     "227 Modo passivo (%u,%u,%u,%u,%d,%d)\r\n",
                     ip[0], ip[1], ip[2], ip[3], DATA_PORT >> 8, DATA_PORT & 0xFF);
            send_str(ctrl, resp);
            pasv_ready = 1;
        } else if (!strcmp(cmd, "LIST")) {
            if (!pasv_ready) {
                send_str(ctrl, "425 Use PASV antes\r\n");
            } else {
                cmd_list(ctrl, data_listen);
                pasv_ready = 0;
            }
        } else if (!strcmp(cmd, "STOR")) {
            if (!pasv_ready) {
                send_str(ctrl, "425 Use PASV antes\r\n");
            } else {
                cmd_stor(ctrl, data_listen, arg);
                pasv_ready = 0;
            }
        } else if (!strcmp(cmd, "DELE")) {
            cmd_dele(ctrl, arg);
        } else {
            send_str(ctrl, "502 Comando nao suportado\r\n");
        }
    }
}

int main(void)
{
    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, sysutil_callback, NULL);

    sysModuleLoad(SYSMODULE_NET);
    netInitialize();

    mkdir(MUSIC_DIR, 0777);   // ignora erro se já existir

    int ctrl_listen = make_listen_socket(CTRL_PORT);
    int data_listen = make_listen_socket(DATA_PORT);
    if (ctrl_listen < 0 || data_listen < 0)
        g_running = 0;

    while (g_running) {
        sysUtilCheckCallback();

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(ctrl_listen, &rfds);
        struct timeval tv = {0, 500000};

        if (select(ctrl_listen + 1, &rfds, NULL, NULL, &tv) > 0) {
            int c = accept(ctrl_listen, NULL, NULL);
            if (c >= 0) {
                handle_session(c, data_listen);
                close(c);
            }
        }
    }

    if (ctrl_listen >= 0) close(ctrl_listen);
    if (data_listen >= 0) close(data_listen);
    netDeinitialize();
    sysModuleUnload(SYSMODULE_NET);
    return 0;
}