// Programme client pour les agences
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_RESPONSE 8192



// Function to send a request and receive a response
void send_request(int sock, char* protocol, char* request, char* response, int response_size, struct sockaddr_in* server_addr) {
    if (strcmp(protocol, "tcp") == 0) {
        if (send(sock, request, strlen(request), 0) < 0) {
            strcpy(response, "Erreur d'envoi");
            return;
        }
        int bytes = recv(sock, response, response_size - 1, 0);
        if (bytes > 0) {
            response[bytes] = '\0';
        } else {
            strcpy(response, "Erreur de réception");
        }
    } else { // udp
        if (sendto(sock, request, strlen(request), 0, (struct sockaddr*)server_addr, sizeof(*server_addr)) < 0) {
            strcpy(response, "Erreur d'envoi");
            return;
        }
        socklen_t addr_len = sizeof(*server_addr);
        int bytes = recvfrom(sock, response, response_size - 1, 0, (struct sockaddr*)server_addr, &addr_len);
        if (bytes > 0) {
            response[bytes] = '\0';
        } else {
            strcpy(response, "Erreur de réception");
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <id_agence> <protocol> (tcp or udp)\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int agency_id = atoi(argv[1]);
    char* protocol = argv[2];

    // Validate protocol
    if (strcmp(protocol, "tcp") != 0 && strcmp(protocol, "udp") != 0) {
        printf("Invalid protocol: %s. Use 'tcp' or 'udp'.\n", protocol);
        exit(EXIT_FAILURE);
    }

    // Create socket based on protocol
    int sock;
    if (strcmp(protocol, "tcp") == 0) {
        sock = socket(AF_INET, SOCK_STREAM, 0);
    } else {
        sock = socket(AF_INET, SOCK_DGRAM, 0);
    }
    if (sock < 0) {
        perror("Erreur lors de la création du socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Connect to server for TCP only
    if (strcmp(protocol, "tcp") == 0) {
        if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            perror("Erreur lors de la connexion au serveur");
            exit(EXIT_FAILURE);
        }
        printf("Connecté au serveur en TCP\n");
    } else {
        printf("Client UDP prêt\n");
    }

    // Menu loop
    while (1) {
        printf("\nChoisissez une option:\n");
        printf("1. Réserver des places\n");
        printf("2. Annuler une réservation\n");
        printf("3. Demander la facture\n");
        printf("4. Consulter les vols\n");
        printf("5. Quitter\n");
        printf("Votre choix: ");

        int choice;
        scanf("%d", &choice);
        getchar(); // Consume newline

        char request[256];
        char response[MAX_RESPONSE];
        if (choice == 1) {
            int ref, seats;
            printf("Entrez la référence du vol: ");
            scanf("%d", &ref);
            printf("Entrez le nombre de places: ");
            scanf("%d", &seats);
            getchar();
            snprintf(request, sizeof(request), "RESERVE %d %d %d", ref, agency_id, seats);
            send_request(sock, protocol, request, response, sizeof(response), &server_addr);
            if (strcmp(response, "SUCCESS") == 0) {
                printf("Réservation réussie\n");
            } else {
                printf("Réponse du serveur: %s\n", response);
            }
        } else if (choice == 2) {
            int ref, seats;
            printf("Entrez la référence du vol: ");
            scanf("%d", &ref);
            printf("Entrez le nombre de places à annuler: ");
            scanf("%d", &seats);
            getchar();
            snprintf(request, sizeof(request), "CANCEL %d %d %d", ref, agency_id, seats);
            send_request(sock, protocol, request, response, sizeof(response), &server_addr);
            if (strcmp(response, "SUCCESS") == 0) {
                printf("Annulation réussie\n");
            } else {
                printf("Réponse du serveur: %s\n", response);
            }
        } else if (choice == 3) {
            snprintf(request, sizeof(request), "INVOICE %d", agency_id);
            send_request(sock, protocol, request, response, sizeof(response), &server_addr);
            printf("Facture: %s\n", response);
        } else if (choice == 4) {
            snprintf(request, sizeof(request), "CONSULT");
            send_request(sock, protocol, request, response, sizeof(response), &server_addr);
            printf("Liste des vols:\n%s", response);
        } else if (choice == 5) {
            break;
        } else {
            printf("Choix invalide\n");
        }
    }

    close(sock);
    printf("Déconnexion\n");
    return 0;
}