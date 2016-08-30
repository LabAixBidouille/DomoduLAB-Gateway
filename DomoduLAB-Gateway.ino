/*
  Version 7.1
  Version opérationnelle
  - avec MQTT
  - avec UC3-LCD
  - protocole 485 amélioré = messages encadrés + CS et concaténables
  - gestion des non conformités 485
  - 485: Files String remplacées par File char.
  - MQTT: Files String remplacées par File char.
  Les émissions MQTT et RS485 s'effectuent via des tables tampons
  où les messages sont stockés provisoirement.
  C'est dans la boucle loop que l'on teste s'il y a un message à transmettre.
  Les lancements d'émission ne sont pas effectuées à la suite d'un traitement
  de message reçu.

  La perte de réseau TCP entraine 3 essais successifs en loop. Si la connexion
  n'est pas retrouvée, une tentative est effectuée toutes les 5 sec. Le GW s'affiche en alarme
*/
#include <avr/pgmspace.h>
const PROGMEM char mV0[] = "";
const PROGMEM char mV1[] = "01=?";
const PROGMEM char mV2[] = "02=?";
const PROGMEM char mV3[] = "03=?";
const PROGMEM char* const messVie[] = {mV0,mV1,mV2,mV3}; 

const PROGMEM char al0[] = "01/AL/FC=1";
const PROGMEM char al1[] = "01/AL/FC=0";
const PROGMEM char al2[] = "01/AL/LN=1";
const PROGMEM char al3[] = "01/AL/LN=0";
const PROGMEM char al4[] = "02/AL/FC=1";
const PROGMEM char al5[] = "02/AL/FC=0";
const PROGMEM char al6[] = "02/AL/LN=1";
const PROGMEM char al7[] = "02/AL/LN=0";
const PROGMEM char al8[] = "03/AL/FC=1";
const PROGMEM char al9[] = "03/AL/FC=0";
const PROGMEM char al10[] = "03/AL/LN=1";
const PROGMEM char al11[] = "03/AL/LN=0";
const PROGMEM char* const AL[] = {al0,al1,al2,al3,al4,al5,al6,al7,al8,al9,al10,al11};

#include <SoftwareSerial.h>
#include <SPI.h>
#include <Ethernet.h>
#include <IPStack.h>
#include <Countdown.h>
#include <MQTTClient.h>

SoftwareSerial RS485(5, 4); // RX, TX

#define SensTrans 6 // RS 485 utilise la broche D6 pour fixer le sens de la transmission serie RS485
#define AlarmLED 7
#define Test 8 // Bouton Test

int arrivedcount = 0;
boolean etatLED = true;
int etatsLogiques = 0;
int UC, uc;

byte essai[] = {0, 0, 0, 0};
int enPanne = 0;
unsigned long reqpreMillis = 0; // déclaration d'une variable pour la temporisation
byte UCeV = 1;

#define F485 0 // utilisé dans les fonctions communes
#define DIM_FILE485 60
bool messEnCours = false; // message En Cours (485)
byte idxFile485 = 0;
char file485[DIM_FILE485];// File des messages 485

#define FMQTT 1
#define DIM_FILEMQTT 50
byte idxFileMQTT = 0;
char fileMQTT[DIM_FILEMQTT];// File des messages MQTT
char messSent[30];

#define DIM_BUFFER 60
char Buffer[DIM_BUFFER]; // Buffer commun d'envoi de message

/**************************
   DECLARATIONS RESEAU/MQTT
 **************************/
EthernetClient c;
IPStack ipstack(c);
MQTT::Client<IPStack, Countdown> client = MQTT::Client<IPStack, Countdown>(ipstack);

//byte mac[] = {  0x00, 0xAA, 0xBB, 0xCC, 0xDE, 0x02 };
//char serverName[] = "aquaponie.local";

const byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };  // fixe l'adresse mac du shield
const byte ip[] = { 192, 168, 0, 20 };                      // fixe l'adresse ip de l'application
//byte ip[] = { 192, 168, 148, 30}                     // fixe l'adresse ip de l'application
byte connectCount = 0;
MQTT::Message message;


//=================================================================
//       FONCTIONS COMMUNES
//=================================================================

byte findFirstMess(byte file) { //Retourne l'index de fin du 1er message
  int i = 0;
  int finMess = 0; // Si la file est vide
  switch (file){
    case 0:
      if (file485[0] == char(0)) return 0;
      else {
        while (file485[i] != char(4)) { // Recherche du caractère \4
          i++;
          if (i == DIM_FILE485) return 0;
        }
        return i+1;
      }
      break;
    case 1:
//    Serial.print("fFM1 ");
//    Serial.println(byte(fileMQTT[0]));
      if (fileMQTT[0] == char(0)) return 0;
      else {
        while (fileMQTT[i] != char(4)) { // Recherche du caractère \4
          //Serial.println(byte(fileMQTT[i]));
          i++;
          if (i == DIM_FILEMQTT) return 0;
        }
        return i+1;
      }
      break;
  }
}

byte findChar(char *chaine, char c, byte start) { //Retourne l'index du caractère 'c'
                                                  // à partir de 'start'
  byte L = strlen(chaine);
  if (L == 0) { return -1; } // Si la file est vide
  else {
    byte i = start;
    while (chaine[i] != c) { // Recherche du caractère c
      i++;
      if (i == DIM_FILEMQTT) return -1;
    }
    return i;
  }
}

void append_file(byte fileID, char *mess) {  // Ajoute un message à la file 485.
  int i = 0;
  switch (fileID){
    case 0:
      if ((idxFile485 + strlen(mess)) <= DIM_FILE485){ // S'il y a la place...
        for (i =0; i < strlen(mess); i++) {file485[i+idxFile485] = mess[i];}
        file485[i+idxFile485] = '\4';
        idxFile485 += i+1;
      }
      break;
    case 1:
      //Serial.println(mess);
      if ((idxFileMQTT + strlen(mess)) <= DIM_FILEMQTT){ // S'il y a la place...
        for (i =0; i < strlen(mess); i++) {fileMQTT[i+idxFileMQTT] = mess[i];}
        fileMQTT[i+idxFileMQTT] = '\4';
        idxFileMQTT += i+1;
      }
      break;
  }
}

void up_file(byte fileID) { // Remonte la file d'un message
  int i = 0;
  int finMess = findFirstMess(fileID);
  if (finMess == 0) {return;}
  switch (fileID) {
    case 0:
      //Serial.println(file485);
      for (i=finMess;i<DIM_FILE485;i++) {
        file485[i-finMess] = file485[i];
        if (file485[i] == char(0)) break;
      }
      idxFile485 -= finMess;
      break;
    case 1:
      for (i=finMess;i<DIM_FILEMQTT;i++) {
        fileMQTT[i-finMess] = fileMQTT[i];
        if (fileMQTT[i] == char(0)) break;
      }
      idxFileMQTT -= finMess;
      break;
  }
}

//=================================================================
//       FONCTIONS DE GESTION MQTT
//=================================================================

bool get_fileMQTT(char *topic, char *payload) {
  int i = 0;
  int finMess = findFirstMess(FMQTT);
  if (finMess == 0) {        // Si la file est vide
    topic[0] = char(0);     // Le résultat est vide
    payload[0] = char(0);
    return false;
  }
  else {
    //    Copie du message dans le résultat
    finMess--;
    for (i=0;i<finMess;i++) {Buffer[i] = fileMQTT[i]; }
    strcpy(messSent,Buffer);
    //    Extraction de topic et payload
    byte i5 = findChar(Buffer, '=', 0);
    for (i = 0;i<i5; i++) {topic[i] = Buffer[i];}
    topic[i5] = char(0);
    for (i = i5+1; i<finMess; i++) {payload[i] = Buffer[i+i5];}
    payload[i+i5] = char(0);
    Serial.println(topic);
    Serial.println(payload);
    return true;
  }
}

int publishMessage(char *topic, char *payload, byte QS) {
//  topicSent = Topic;
//  payloadSent = Payload;
  //  Serial.println("Emission  MQTT: " + Topic + " " + Payload);
//  // Transforme les 'String' en tables de 'char'
//  int TopicLen = strlen(topic) + 1;
//  char Topique[TopicLen];
//  Topic.toCharArray(Topique, TopicLen);
//  int PayloadLen = Payload.length() + 1;
//  char buf[PayloadLen];
//  Payload.toCharArray(buf, PayloadLen);
  if (QS == 0) {message.qos = MQTT::QOS0;}
  if (QS == 1) {message.qos = MQTT::QOS1;}
  if (QS == 2) {message.qos = MQTT::QOS2;}
  message.retained = false;
  message.dup = false;
  message.payload = (void*)payload;
  message.payloadlen = strlen(payload);
  int rc = client.publish(topic, message);
  if (QS == 0) {
    client.yield(200); // timeout_ms = 1000
  }
  if (QS == 1) {
    while (arrivedcount == 1)
    {
      client.yield(200);
    }
  }
  if (QS == 2) {
    while (arrivedcount == 2)
    {
      client.yield(200);
    }
  }
  return rc;
}

void EmissionMQTT(){ // Transmet au broker un message s'il existe en table
  char tp[12];
  char pl[25];
  if (get_fileMQTT(tp,pl)) {
//    Serial.println(tp);
//    up_file(FMQTT);
//    publishMessage(tp, pl, 0);
  }
}

//=================================================================
//       FONCTIONS DE LECTURE PROGMEM
//=================================================================

void lectureProgmem(char *dest, byte UC, byte num) {
  switch (num){
    case 8:
      strcpy_P(dest, (char*)pgm_read_byte(&(messVie[UC])));
      break;
    default:
      byte i = (UC-1)*4 + num;
      strcpy_P(dest, (char*)pgm_read_word(&(AL[i])));
      break;
  }
}

//=================================================================
//       FONCTIONS DE GESTION RS485
//=================================================================
bool get_file485(char *dest) {// Charge la chaine dest avec le message en tête de file485
                              // avec sa mise en forme.
  int i = 0;
  int finMess = findFirstMess(F485)-1;
  if (finMess < 0) {        // Si la file est vide
    dest[0] = char(0);      // Le résultat est vide
    return false;
  }
  else {
    // Mise en forme du message à envoyer
    //    Calcul du CheckSum
    int CS = 0;
    for (i = 0; i < finMess; i++) {
      CS += byte(file485[i]);
    }
    //    Copie du message dans le résultat
    for (i=0;i<finMess;i++) {
      dest[i+1] = file485[i];
    }
    //    Ajout des caractères délimiteurs
    dest[0] = char(2);
    dest[finMess+1] = char(3);
    dest[finMess+2] = char(CS);
    dest[finMess+3] = char(4);
    dest[finMess+4] = char(0);
    return true;
  }
}

void RAS_TVB(){  // Exécuté quand RAS et tout va bien
  up_file(F485);
  essai[UC] = 0; // Raz du processus de test de vie
  // Il était en panne ? -> il ne l'est plus, le broker en est informé
  if (bitRead(enPanne, UC) == 1) { // enPanne est l'entier qui rassemble les états d'alarme
    lectureProgmem(Buffer, UC, 2);
    append_file(FMQTT,Buffer);
    bitClear(enPanne, UC);
  }
}

void PB_Liaison(){
  if (essai[UC] < 3) essai[UC]++;
  //  Serial.print(" Pb liaison UC");
  //  Serial.print(UC);
  //  Serial.print(" ");
  //  Serial.println(essai[UC]);
  if (essai[UC] == 3) {
    lectureProgmem(Buffer, UC, 3);
    append_file(FMQTT,Buffer); // Alarme vers le broker 'liaison UC en panne'
    up_file(F485);
  }
}

void PB_Vie(){
  if (essai[UC] < 3) essai[UC]++;
//    Serial.print(" Essai de connexion UC");
//    Serial.print(UC);
//    Serial.print(" ");
//    Serial.println(essai[UC]);
  if ((essai[UC] == 3) && (bitRead(enPanne, UC) == 0)) {
    lectureProgmem(Buffer, UC, 0);
    //Serial.println(Buffer);
    //append_file(FMQTT,Buffer); // Alarme vers le broker 'en panne'
    up_file(F485);
    //Serial.println(file485);
    bitSet(enPanne, UC);
  }
}

void traiteMessage(char *message){ // Traitement local de messages 485
  // Traitement des messages reçus
//    Serial.print(" Reponse: ");
//    Serial.println(message);
  if (strstr(message, "OK")>0){RAS_TVB();}
  else if  (strstr(message, "KO")>0){PB_Liaison();}
  else  {
    RAS_TVB();
    //append_file(FMQTT, message); //Si la file est pleine le message est perdu
  }
}

void Emission485(){ // Emet un message en 485 et gère la réponse
  if (get_file485(Buffer)==true){
    messEnCours = true; // Bloque la possibilité d'autre émission 485 durant le processus E/R

          Serial.print("Emission  485: ");
          Serial.println(Buffer);
    UC = byte(Buffer[2])-48; // Ne fonctionne qu'avec des UC numérotés (01, 02 etc)

    digitalWrite(SensTrans, HIGH);  // transmission RS485 en emission
    delay (1);
    RS485.write(Buffer);
    delay (30); // temporisation 300ms pour permettre le départ de tous les caractères
    digitalWrite(SensTrans, LOW);  // transmission RS485 en reception
    for (byte i = 0;i<DIM_BUFFER;i++){Buffer[i]=char(0);} // RAZ du Buffer

    delay(800); // permet d'attendre un temps de réaction de l'UC
    // et que tous les caractères soient entrés dans le Buffer.

    // Réception de la réponse de l'UC
    // Récupération du message
    byte im = 0;
    while (RS485.available()) // tant que des caractères sont disponibles
    {
      byte c = RS485.read();
      if (c > 0) {
      Buffer[im] = char(c);
      im++;
      }
      delay(2);
    }

    // Extraction et vérification de la conformité
    byte LF = strlen(Buffer);
    if ( LF != 0) {
      byte i2 = 0;
      byte i3 = 0;
      byte i4 = 0;
      while (i4 < LF - 1) {
        byte i = i4;
        i2 = findChar(Buffer, '\2', i);
        i3 = findChar(Buffer, '\3', i);
        i4 = findChar(Buffer, '\4', i);
        i4++;
        if ((i2 >= 0) && (i3 >= 0) && (i4 != -1)) {// Delimiteurs OK
          byte CS = Buffer[i3+1];
          byte cs = 0;
          for (byte n = i2+1; n<i3; n++){
            cs += Buffer[n];
          }
          if ((i2 != i3) && (cs == CS)){  // CheckSum OK
            uc = byte(Buffer[2])-48;
            if (uc == UC) // L'UC est vivant et a été reconnu
            {
              traiteMessage(Buffer);
            }
          }
          else {
            PB_Liaison(); // Non conformité du message reçu
            break; //sortir de la boucle while en cas de multi-messages
          }
        }
        else {
          PB_Liaison(); // Non conformité du message reçu
          break;
        }
      }
    }
    else {
      PB_Vie(); // Pas de réponse
    }
    messEnCours = false; //Libère la possibilité d'émission 485
  }
}

/***************************
   GESTION MESSAGES ENTRANTS
 ***************************/
void messageArrived(MQTT::MessageData& md){
  MQTT::Message &message = md.message;
  int PLen = message.payloadlen;
  int TLen = md.topicName.lenstring.len;

  // Construction de la chaine à partir du topic et du payload
//  char mp[TLen + PLen + 2];
//  for (byte i = 0; i < TLen; i++) {mp[i] = md.topicName.lenstring.data[i];}
//        /*  Cette séquence est à travailler pour rester en char */
//  String topicReceived = String(mp).substring(0, TLen);
//  mp[TLen] = '=';
//  char pl[PLen + 1];
//  String ch = String((char*)message.payload).substring(0, PLen);
//  ch.toCharArray(pl, PLen + 1);
//  for (byte i = 0; i < PLen + 1; i++) {mp[i + TLen + 1] = pl[i];}
//  mp[TLen + PLen + 2] = '\0';

  char topicReceived[TLen + 1];
  strcpy(Buffer,md.topicName.lenstring.data);
  strcpy(topicReceived,md.topicName.lenstring.data);
  Buffer[TLen] = '=';
  char pl[PLen + 1];
  String ch = String((char*)message.payload).substring(0, PLen);
  ch.toCharArray(pl, PLen + 1);
  for (byte i = 0; i < PLen + 1; i++) {Buffer[i + TLen + 1] = pl[i];}
  Buffer[TLen + PLen + 2] = '\0';

  // N'accepte pas (filtre) les messages identiques à ceux envoyés
  // càd les retours liés à la souscription
  if (strcmp(Buffer, messSent) == 0) return;

  //  Serial.print("Reception MQTT: ");
  //  Serial.println(mp);

  //Traitement d'une demande au GW
  if (topicReceived == "00/RQ") {
    char mess[] = "00/EL=";
    String eP = String(enPanne);
    int payloadLen = eP.length() + 1;
    char payload[payloadLen];
    eP.toCharArray(payload, payloadLen);
    strcat(mess, payload);
    append_file(FMQTT,mess) ;
  }
  /*  bit 0 = GW perte réseau TCP       |
      bit 1 = UC1 ne répond pas         |
      bit 2 = UC2 ne répond pas         | allument la led d'alarme
      ...                               |
      bit 8 = dysfonctionnement sur GW  |
      bit 9 = dysfonctionnement sur UC1
      bit 10 = dysfonctionnement sur UC2
      ...
  */
  else {append_file(F485,Buffer); }// vers 485
}

/**************************
   CONNEXIONS RESEAU/MQTT
 **************************/
void connect(){
  char hostname[] = "192.168.0.22"; // IP du Broker
  int port = 1883;

  Serial.print("Connexion au Reseau ");

  int rc = ipstack.connect(hostname, port);
  if (rc != 1)
  {
    Serial.print("TCP = ");
    Serial.println(rc);
  }
  else
  {
    Serial.println("TCP Ok");
    connectCount = 0;

    Serial.print("Connexion au Broker ");
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
    data.MQTTVersion = 3;
    data.clientID.cstring = (char*)"GW_V7";
    rc = client.connect(data);
    if (rc != 0)
    {
      Serial.print("MQTT = ");
      Serial.println(rc);
    }
    else
    {
      Serial.println("MQTT Ok");
      rc = client.subscribe("00/#", MQTT::QOS2, messageArrived); // 00 est le numéro du GW
      rc += client.subscribe("01/#", MQTT::QOS2, messageArrived); // 01 est le numéro de l'UC1
      rc += client.subscribe("02/#", MQTT::QOS2, messageArrived); // 02 est le numéro de l'UC2
      rc += client.subscribe("03/#", MQTT::QOS2, messageArrived); // 03 est le numéro de l'UC3
      if (rc != 0)
      {
        Serial.print("MQTT subscribe = ");
        Serial.println(rc);
      }
      else  Serial.println("MQTT subscribe Ok");
    }
  }
}

void setup(){
  Serial.begin(9600); //Lancer le mode serie
  Serial.println("GW_V7.1");

  pinMode(SensTrans, OUTPUT); // Sens Transmission sur broche D6
  pinMode(AlarmLED, OUTPUT);
  pinMode(Test, INPUT_PULLUP);
  digitalWrite(SensTrans, LOW);

  Ethernet.begin(mac, ip);
  delay(1000);
  connect();
  
  RS485.begin(4800); // set the data rate for the SoftwareSerial port
  reqpreMillis = millis();

  for (byte i = 0;i<DIM_FILE485;i++){file485[i]=char(0);}
  for (byte i = 0;i<DIM_FILEMQTT;i++){fileMQTT[i]=char(0);}
}

void loop(){
  if (client.isConnected()){
    arrivedcount = 0;
    client.yield(500);
  }
  else if (connectCount < 3) { // 3 tentatives  de connexion consécutives, puis toutes les 5 sec.
    connect();
    connectCount ++;
  }

  unsigned long reqcurMillis = millis();
  if (reqcurMillis - reqpreMillis > 5000) // test si 5 secondes se sont écoulées
  {
    reqpreMillis = reqcurMillis;
    if (UCeV == 4) {
      UCeV = 1; // reboucle dans la table des messages de vie
    }
    if (connectCount == 3) connect(); // Prend le relais des tentatives de connexion TCP
    lectureProgmem(Buffer, UCeV, 8);
    append_file(F485, Buffer); // message de vie enregistré en Progmem
    UCeV ++;
  }
  
  if (enPanne) {  // Gestion Led d'alarmes
    digitalWrite(AlarmLED, HIGH);}
  else {
    digitalWrite(AlarmLED, LOW);}

  // Si les tables tampons contiennent un message à transmettre,
  // les fonctions suivantes les mettre en oeuvre vers les UC ou vers le broker
  //EmissionMQTT();                      // Lancement d'une émission MQTT
  if (messEnCours == false) {Emission485();}  // Lancement d'une émission 485
  delay(300);
}




