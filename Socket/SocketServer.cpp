//
// Created by arturocv on 20/09/17.
//

#include "SocketServer.h"
#include "../Servers/Server.h"


SocketServer::SocketServer() {
}

void SocketServer::setPort(int port) {
    SocketServer::port = port;
}

/// Metodo que crea el socket
/// \return True si el socket fue creado correctamente, false si ocurrio algun error
bool SocketServer::createSocket() {
    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); //Se inicializa el serverSocket
    if(serverSocket < 0) //Si es cero ocurrio un error a la hora de crear el socket
        return false;
    info.sin_family = AF_INET;
    info.sin_addr.s_addr = INADDR_ANY; //Restringe las conexiones al socket
    info.sin_port = htons(port); //Puerto del socket
    memset(&info.sin_zero, 0, sizeof(info.sin_zero)); //Limpia la estructura
    return true; //Se ha creado el socket correctamente
}

/// Liga el socket con el sistema operativo
/// \return
bool SocketServer::attachToSO() {
    if((bind(serverSocket, (sockaddr *) &info, (socklen_t) sizeof(info))) < 0)
        return false; //Si la funcion bind retorna un numero negativo es que ocurrio un error

    int listen_return = listen(serverSocket, 10); //Establece el maximo de clients que va a tener el servidor
    if(listen_return < 0)
        return false;

    return true; //Se ha ligado el socket correctamente
}

/// Metodo que administra las solicitudes de los clientes
/// \param clientData Informacion del cliente en cuestion
/// \return
void* SocketServer::clientManager(void *clientData) {
    DataSocket *client = (DataSocket*)clientData; //Cliente
    while (true) {

        string message; //Variable que guarda el mensaje recibido
        while (true) { //Ciclio infinito que se encarga de leer el mensaje recibido
            char buffer[10] = {0}; //Se leen 10 caracteres
            int bytes = recv(client->descriptor, buffer, 10, 0); //Se lee el mensaje entrante

            if (bytes <= 0) { //La conexion con el cliente se ha cerrado
                close(client->descriptor); //Se cierra el socket
                pthread_exit(NULL); //Se elimina el hilo
            }

            message.append(buffer, bytes); //Se agregan las cadenas leidas a la variable que guarda el mensaje
            if (bytes < 10) //Caso en el que se ha leido el message entero
                break; //Se finaliza el ciclo
        }

        if (message.length() > 0) { //Caso en el que el mensaje entrante es distinto de nulo

            LinkedList<char *> msg = splitMessage(message); //Se separa el mensaje recibido en diferentes elementos

            char *action = msg.getElement(0)->getData(); //La accion a realizar es el primer elemento de la lista

            if (strcmp(action, "store") == 0) {  //Se trata de una solicitud de almacenamiento
                RMRef_H *ref;

                char *key = msg.getElement(1)->getData(); //Se obtiene la key
                char *value = msg.getElement(2)->getData(); //Se obtiene el value
                char *charValueSize = msg.getElement(3)->getData(); //Se obtiene el value_size, es tipo char.
                int value_size = atoi(charValueSize);

                ref = new RMRef_H(key, value, value_size); //Se crea la estructura de almacenamiento
                MemoryManager *memoryManager = MemoryManager::getInstance();
                bool result = memoryManager->insertElement(ref);

                //Se sincriza el servidor pasivo
                ActiveServer* activeServer = Server::getInstance()->getActiveServer(); //Se obtiene la instancia del servidor activo
                if (activeServer != nullptr) { //El servidor activo no es nulo
                    DataSocket* pasiveServer = activeServer->getServerHA(); //Se obtiene la instancia del servidor pasivo
                    if(pasiveServer != nullptr) //El servidor pasivo no es nulo
                        sendMessage((char *) message.c_str(), pasiveServer); //Se envia el mensaje recibido al servidor pasivo
                }

                //Se envia la respuesta al cliente
                if(result == true){
                    sendMessage("stored", client); //Se ha guardado la referencia
                } else {
                    sendMessage("keyInUse", client); //El Key de la referencia estaba en uso
                }

            } else if (strcmp(action, "erase") == 0) { //Se trata de una solicitud de eliminacion
                char *key = msg.getElement(1)->getData(); //Se obtiene la key de la referencia recibida

                MemoryManager *memoryManager = MemoryManager::getInstance(); //Se obtiene la memoria
                memoryManager->deleteElement(key); //Se realiza la eliminacion

                //Se sincriza el servidor pasivo
                ActiveServer* activeServer = Server::getInstance()->getActiveServer(); //Se obtiene la instancia del servidor activo
                if (activeServer != nullptr) { //El servidor activo no es nulo
                    DataSocket* pasiveServer = activeServer->getServerHA(); //Se obtiene la instancia del servidor pasivo
                    if(pasiveServer != nullptr) //El servidor pasivo no es nulo
                        sendMessage((char *) message.c_str(), pasiveServer); //Se envia el mensaje recibido al servidor pasivo
                }

                //Se envia la respuesta al cliente
                sendMessage("erased", client);

            } else if (strcmp(action, "get") == 0) {
                char *key = msg.getElement(1)->getData(); //Se obtiene la key de la referencia recibida

                MemoryManager *memoryManager = MemoryManager::getInstance();
                RMRef_H *ref_h = memoryManager->getElement(key); //Se intenta obtener el elemento

                if (ref_h != nullptr) {
                    string response = "obtained,";
                    response.append(ref_h->createString()); //Se añade el string de la referencia
                    sendMessage((char *) response.c_str(), client); //Se envia
                } else { //No se ha encontrado la referencia
                    sendMessage("notFound", client);
                }

            } else if (strcmp(action, "pasiveserver") == 0) { //se recibe un mensaje del servidor pasivo
                PasiveServer* pasiveServer = Server::getPasiveServer();
                if(pasiveServer == nullptr) {
                    Server::getInstance()->getActiveServer()->setServerHA(client); //Se guarda el cliente especifico del servidor de respaldo

                    //Se crea un hilo para sincronizar el servidor activo con los datos del pasivo
                    int memorySize = MemoryManager::getSize();

                    if(memorySize > 0){ //Se sincroniza el servidor pasivo
                        pthread_t syncronizePS;
                        pthread_create(&syncronizePS, 0, ActiveServer::syncronizePS, client);
                        pthread_detach(syncronizePS);
                    } else if(memorySize == 0) { //Se sincroniza el servidor activo
                        pthread_t syncronizeAS;
                        pthread_create(&syncronizeAS, 0, ActiveServer::syncronize, client);
                        pthread_detach(syncronizeAS);
                    }
                    break;
                }
            }
        }
        message = {0}; //Se limpia el message
    }

    pthread_exit(NULL); //Se elimina el hilo
}

/// Metodo para enviar un mensaje a los clientes
/// \param msn Mensaje a enviar
void SocketServer::sendMessage(const char *msn, DataSocket *client) {
    send(client->descriptor, msn, strlen(msn), 0);
}

/// Metodo para separar el mensaje recibido en los elementos dividos por comas
/// \param message Mensaje entrante
/// \return Lista con los elementos divididos
LinkedList<char*> SocketServer::splitMessage(string message) {
    LinkedList<char*> list = LinkedList<char*>(); //Lista en la que se guardar los elementos
    char *charRef = strdup(message.c_str()); //Se transforma el mensaje a char*
    char* element = strtok(charRef, ","); //Separa el char cuando lea la coma;
    while (element != NULL) {
        list.insertAtEnd(element); // Se guarda el dato en la lista
        element = strtok (NULL, ",");  // Separa el resto de la cadena cuando lea la coma
    }

    return list;
}

///Metodo para cerrar el servidor
void SocketServer::closeSocket() {
    for (int i = 0; i < clients.getSize(); ++i) {
        close(clients.getElement(i)->getData()->descriptor);
    }
    close(serverSocket);
}