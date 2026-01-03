// Programme serveur pour le système de réservation de vols
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Structures de données globales
Flight flights[MAX_FLIGHTS];
int num_flights = 0;
double total_payments[MAX_AGENCIES + 1] = {0}; // Index 0 inutilisé
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// Charge les vols depuis vols.txt
void load_flights() {
    FILE *fp = fopen("vols.txt", "r");
    if (!fp) {
        perror("Erreur lors de l'ouverture de vols.txt");
        exit(EXIT_FAILURE);
    }
    char line[256];
    while (fgets(line, sizeof(line), fp) && num_flights < MAX_FLIGHTS) {
        sscanf(line, "%d %s %d %d", 
               &flights[num_flights].ref, 
               flights[num_flights].destination, 
               &flights[num_flights].available_seats, 
               &flights[num_flights].price);
        num_flights++;
    }
    fclose(fp);
    printf("Vols chargés: %d\n", num_flights);
}

// Rejoue les transactions historiques depuis histo.txt
void replay_history() {
    FILE *fp = fopen("histo.txt", "r");
    if (!fp) return; // Fichier peut ne pas exister au démarrage
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        int ref, agency_id, value;
        char transaction[20], result[20];
        sscanf(line, "%d %d %s %d %s", &ref, &agency_id, transaction, &value, result);
        
        int flight_idx = -1;
        for (int i = 0; i < num_flights; i++) {
            if (flights[i].ref == ref) {
                flight_idx = i;
                break;
            }
        }
        
        if (flight_idx != -1) {
            if (strcmp(transaction, "Demande") == 0 && strcmp(result, "succès") == 0) {
                flights[flight_idx].available_seats -= value;
                total_payments[agency_id] += value * flights[flight_idx].price;
            } else if (strcmp(transaction, "Annulation") == 0) {
                flights[flight_idx].available_seats += value;
                total_payments[agency_id] -= 0.9 * value * flights[flight_idx].price;
            }
        }
    }
    fclose(fp);
}

// Met à jour facture.txt avec les paiements totaux
void update_facture() {
    FILE *fp = fopen("facture.txt", "w");
    if (!fp) {
        perror("Erreur lors de l'ouverture de facture.txt");
        return;
    }
    for (int i = 1; i <= MAX_AGENCIES; i++) {
        if (total_payments[i] != 0) {
            fprintf(fp, "%d %.2f\n", i, total_payments[i]);
        }
    }
    fclose(fp);
}

// Met à jour vols.txt après chaque transaction
void update_vols() {
    FILE *fp = fopen("vols.txt", "w");
    if (!fp) {
        perror("Erreur lors de l'ouverture de vols.txt pour écriture");
        return;
    }
    for (int i = 0; i < num_flights; i++) {
        fprintf(fp, "%d %s %d %d\n", flights[i].ref, flights[i].destination, 
                flights[i].available_seats, flights[i].price);
    }
    fclose(fp);
}

// Trouve l'index d'un vol par sa référence
int find_flight_index(int ref) {
    for (int i = 0; i < num_flights; i++) {
        if (flights[i].ref == ref) {
            return i;
        }
    }
    return -1;
}

// Traite les requêtes des clients TCP dans un thread séparé
void* handle_client(void* arg) {
    int client_sock = *(int*)arg;
    free(arg);
    char buffer[256];

    while (1) {
        int bytes = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';

        pthread_mutex_lock(&mutex);

        char command[20];
        sscanf(buffer, "%s", command);

        if (strcmp(command, "RESERVE") == 0 || strcmp(command, "CANCEL") == 0) {
            int ref, agency_id, value;
            int parsed = sscanf(buffer + strlen(command), "%d %d %d", &ref, &agency_id, &value);
            if (parsed != 3) {
                send(client_sock, "INVALID_COMMAND", 15, 0);
                pthread_mutex_unlock(&mutex);
                continue;
            }

            FILE *histo_fp = fopen("histo.txt", "a");
            if (!histo_fp) {
                send(client_sock, "SERVER_ERROR", 12, 0);
                pthread_mutex_unlock(&mutex);
                continue;
            }

            int flight_idx = find_flight_index(ref);
            if (strcmp(command, "RESERVE") == 0) {
                if (flight_idx != -1 && flights[flight_idx].available_seats >= value) {
                    flights[flight_idx].available_seats -= value;
                    total_payments[agency_id] += value * flights[flight_idx].price;
                    fprintf(histo_fp, "%d %d Demande %d succès\n", ref, agency_id, value);
                    send(client_sock, "SUCCESS", 7, 0);
                    update_vols();
                } else {
                    fprintf(histo_fp, "%d %d Demande %d impossible\n", ref, agency_id, value);
                    send(client_sock, "FAILURE", 7, 0);
                }
            } else { // CANCEL
                if (flight_idx != -1) {
                    flights[flight_idx].available_seats += value;
                    total_payments[agency_id] -= 0.9 * value * flights[flight_idx].price;
                    fprintf(histo_fp, "%d %d Annulation %d succès\n", ref, agency_id, value);
                    send(client_sock, "SUCCESS", 7, 0);
                    update_vols();
                } else {
                    send(client_sock, "FAILURE", 7, 0);
                }
            }
            fclose(histo_fp);
        } else if (strcmp(command, "INVOICE") == 0) {
            int agency_id;
            int parsed = sscanf(buffer + strlen(command), "%d", &agency_id);
            if (parsed != 1) {
                send(client_sock, "INVALID_COMMAND", 15, 0);
                pthread_mutex_unlock(&mutex);
                continue;
            }
            char response[50];
            snprintf(response, sizeof(response), "INVOICE %.2f", total_payments[agency_id]);
            send(client_sock, response, strlen(response), 0);
        } else if (strcmp(command, "CONSULT") == 0) {
            char response[8192] = "";
            for (int i = 0; i < num_flights; i++) {
                char flight_info[256];
                snprintf(flight_info, sizeof(flight_info), "%d %s %d %d\n",
                         flights[i].ref, flights[i].destination,
                         flights[i].available_seats, flights[i].price);
                strcat(response, flight_info);
            }
            send(client_sock, response, strlen(response), 0);
        } else {
            send(client_sock, "UNKNOWN_COMMAND", 15, 0);
        }

        update_facture();
        pthread_mutex_unlock(&mutex);
    }

    close(client_sock);
    return NULL;
}

// Traite les requêtes UDP des agences
void handle_agency_udp(int agency_sock) {
    char buffer[256];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    while (1) {
        int bytes = recvfrom(agency_sock, buffer, sizeof(buffer) - 1, 0, 
                            (struct sockaddr*)&client_addr, &client_len);
        if (bytes <= 0) continue;
        buffer[bytes] = '\0';

        pthread_mutex_lock(&mutex);

        char command[20];
        sscanf(buffer, "%s", command);

        if (strcmp(command, "RESERVE") == 0 || strcmp(command, "CANCEL") == 0) {
            int ref, agency_id, value;
            int parsed = sscanf(buffer + strlen(command), "%d %d %d", &ref, &agency_id, &value);
            if (parsed != 3) {
                sendto(agency_sock, "INVALID_COMMAND", 15, 0, 
                       (struct sockaddr*)&client_addr, client_len);
                pthread_mutex_unlock(&mutex);
                continue;
            }

            FILE *histo_fp = fopen("histo.txt", "a");
            if (!histo_fp) {
                sendto(agency_sock, "SERVER_ERROR", 12, 0, 
                       (struct sockaddr*)&client_addr, client_len);
                pthread_mutex_unlock(&mutex);
                continue;
            }

            int flight_idx = find_flight_index(ref);
            if (strcmp(command, "RESERVE") == 0) {
                if (flight_idx != -1 && flights[flight_idx].available_seats >= value) {
                    flights[flight_idx].available_seats -= value;
                    total_payments[agency_id] += value * flights[flight_idx].price;
                    fprintf(histo_fp, "%d %d Demande %d succès\n", ref, agency_id, value);
                    sendto(agency_sock, "SUCCESS", 7, 0, 
                           (struct sockaddr*)&client_addr, client_len);
                    update_vols();
                } else {
                    fprintf(histo_fp, "%d %d Demande %d impossible\n", ref, agency_id, value);
                    sendto(agency_sock, "FAILURE", 7, 0, 
                           (struct sockaddr*)&client_addr, client_len);
                }
            } else { // CANCEL
                if (flight_idx != -1) {
                    flights[flight_idx].available_seats += value;
                    total_payments[agency_id] -= 0.9 * value * flights[flight_idx].price;
                    fprintf(histo_fp, "%d %d Annulation %d succès\n", ref, agency_id, value);
                    sendto(agency_sock, "SUCCESS", 7, 0, 
                           (struct sockaddr*)&client_addr, client_len);
                    update_vols();
                } else {
                    sendto(agency_sock, "FAILURE", 7, 0, 
                           (struct sockaddr*)&client_addr, client_len);
                }
            }
            fclose(histo_fp);
        } else if (strcmp(command, "INVOICE") == 0) {
            int agency_id;
            int parsed = sscanf(buffer + strlen(command), "%d", &agency_id);
            if (parsed != 1) {
                sendto(agency_sock, "INVALID_COMMAND", 15, 0, 
                       (struct sockaddr*)&client_addr, client_len);
                pthread_mutex_unlock(&mutex);
                continue;
            }
            char response[50];
            snprintf(response, sizeof(response), "INVOICE %.2f", total_payments[agency_id]);
            sendto(agency_sock, response, strlen(response), 0, 
                   (struct sockaddr*)&client_addr, client_len);
        } else if (strcmp(command, "CONSULT") == 0) {
            char response[8192] = "";
            for (int i = 0; i < num_flights; i++) {
                char flight_info[256];
                snprintf(flight_info, sizeof(flight_info), "%d %s %d %d\n",
                         flights[i].ref, flights[i].destination,
                         flights[i].available_seats, flights[i].price);
                strcat(response, flight_info);
            }
            sendto(agency_sock, response, strlen(response), 0, 
                   (struct sockaddr*)&client_addr, client_len);
        } else {
            sendto(agency_sock, "UNKNOWN_COMMAND", 15, 0, 
                   (struct sockaddr*)&client_addr, client_len);
        }

        update_facture();
        pthread_mutex_unlock(&mutex);
    }
}

// Gère les connexions des agences dans un thread séparé
void* agency_handler(void* arg) {
    char* protocol = (char*)arg;
    int agency_sock;
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(8080);

    if (strcmp(protocol, "tcp") == 0) {
        agency_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (agency_sock < 0) {
            perror("Erreur lors de la création du socket TCP");
            exit(EXIT_FAILURE);
        }
        if (bind(agency_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            perror("Erreur lors du bind TCP");
            exit(EXIT_FAILURE);
        }
        if (listen(agency_sock, 10) < 0) {
            perror("Erreur lors du listen TCP");
            exit(EXIT_FAILURE);
        }
        printf("Serveur agence (TCP) démarré sur le port 8080\n");

        while (1) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int* client_sock = malloc(sizeof(int));
            *client_sock = accept(agency_sock, (struct sockaddr*)&client_addr, &client_len);
            if (*client_sock < 0) {
                perror("Erreur lors de l'acceptation");
                free(client_sock);
                continue;
            }

            pthread_t thread;
            if (pthread_create(&thread, NULL, handle_client, client_sock) != 0) {
                perror("Erreur lors de la création du thread");
                close(*client_sock);
                free(client_sock);
            } else {
                pthread_detach(thread);
            }
        }
    } else { // udp
        agency_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (agency_sock < 0) {
            perror("Erreur lors de la création du socket UDP");
            exit(EXIT_FAILURE);
        }
        if (bind(agency_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            perror("Erreur lors du bind UDP");
            exit(EXIT_FAILURE);
        }
        printf("Serveur agence (UDP) démarré sur le port 8080\n");
        handle_agency_udp(agency_sock);
    }
    close(agency_sock);
    return NULL;
}

int main() {
    // Initialisation
    load_flights();
    replay_history();

    // Choix du protocole
    printf("Choisissez le protocole pour les agences (tcp/udp): ");
    char protocol[10];
    fgets(protocol, sizeof(protocol), stdin);
    protocol[strcspn(protocol, "\n")] = 0; // Supprime le retour à la ligne

    if (strcmp(protocol, "tcp") != 0 && strcmp(protocol, "udp") != 0) {
        printf("Protocole invalide. Utilisation de TCP par défaut.\n");
        strcpy(protocol, "tcp");
    }

    // Démarre le gestionnaire des agences dans un thread séparé
    pthread_t agency_thread;
    if (pthread_create(&agency_thread, NULL, agency_handler, protocol) != 0) {
        perror("Erreur lors de la création du thread agence");
        exit(EXIT_FAILURE);
    }
    pthread_detach(agency_thread);

    // Console admin
    while (1) {
        printf("Admin> ");
        char command[100];
        fgets(command, sizeof(command), stdin);
        command[strcspn(command, "\n")] = 0;

        pthread_mutex_lock(&mutex);
        
        if (strncmp(command, "flight ", 7) == 0) {
            int ref;
            sscanf(command + 7, "%d", &ref);
            int found = 0;
            for (int i = 0; i < num_flights; i++) {
                if (flights[i].ref == ref) {
                    printf("Vol %d: %s, %d places, %d€/place\n",
                           ref, flights[i].destination, flights[i].available_seats, 
                           flights[i].price);
                    found = 1;
                    break;
                }
            }
            if (!found) printf("Vol %d non trouvé\n", ref);
        } else if (strncmp(command, "invoice ", 8) == 0) {
            int agency_id;
            sscanf(command + 8, "%d", &agency_id);
            printf("Facture agence %d: %.2f€\n", agency_id, total_payments[agency_id]);
        } else if (strcmp(command, "history") == 0) {
            FILE *fp = fopen("histo.txt", "r");
            if (fp) {
                char line[256];
                while (fgets(line, sizeof(line), fp)) {
                    printf("%s", line);
                }
                fclose(fp);
            } else {
                printf("Historique non trouvé\n");
            }
        } else if (strcmp(command, "exit") == 0) {
            pthread_mutex_unlock(&mutex);
            printf("Arrêt du serveur\n");
            exit(0);
        } else {
            printf("Commande inconnue. Options: flight <ref>, invoice <agency_id>, history, exit\n");
        }
        
        pthread_mutex_unlock(&mutex);
    }

    pthread_mutex_destroy(&mutex);
    return 0;
}