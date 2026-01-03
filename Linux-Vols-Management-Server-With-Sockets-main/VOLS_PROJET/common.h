#ifndef COMMON_H
#define COMMON_H

#define MAX_FLIGHTS 100
#define MAX_AGENCIES 100

typedef struct {
    int ref;              // Flight reference number
    char destination[50]; // Flight destination
    int available_seats;  // Number of available seats
    int price;            // Price per seat
} Flight;

#endif