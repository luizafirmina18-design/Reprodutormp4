#include <stdio.h>
#include <string.h>
#include <sys/process.h>
#include <cell/sysmodule.h>
#include <sysutil/sysutil.h>

SYS_PROCESS_PARAM(1001, 0x10000);

char videos[20][512];
int total_videos = 0;

void carregar_videos() {
    FILE *f = fopen("/dev_hdd0/game/YOUP00001/USRDIR/videos.txt", "r");
    if (!f) {
        strcpy(videos[0], "https://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4");
        total_videos = 1;
        return;
    }
    char linha[512];
    while (fgets(linha, sizeof(linha), f) && total_videos < 20) {
        linha[strcspn(linha, "\n")] = 0;
        if (strlen(linha) > 5) {
            strcpy(videos[total_videos], linha);
            total_videos++;
        }
    }
    fclose(f);
}

int main() {
    printf("Yo! Player - Player da Comunidade\n");
    carregar_videos();
    printf("Total de videos: %d\n", total_videos);
    if (total_videos > 0) {
        printf("Abrindo: %s\n", videos[0]);
        sysutil_register_callback(0, NULL, NULL);
    }
    while(1) {
        sysutil_check_callback();
    }
    return 0;
}