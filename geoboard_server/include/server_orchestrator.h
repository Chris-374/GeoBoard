#ifndef SERVER_ORCHESTRATOR_H
#define SERVER_ORCHESTRATOR_H

/*
 * server_orchestrator.h
 *
 * API del servidor principal rank 1.
 * El servidor recibe la imagen cifrada del cliente, la descifra, la divide
 * en 9 regiones y asigna 3 regiones a cada worker.
 */

int server_main(int world_size);

#endif
