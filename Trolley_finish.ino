/*
  Котроллер готовности на финише троллея.
 */

#include <SPI.h>
#include <Ethernet.h>
#include <Wiegand.h>

//  MAC адрес устройства (должен быть уникален в сети
byte macaddr[] = {
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED
};
// IP адрес этого устройства
IPAddress myip(192, 168, 0, 30);

//IP адрес устройства на старте
IPAddress serverip(192,168,0,235);


// сетевой интерфейс
EthernetClient ethclient;

// WIEGAND интерфейс
WIEGAND wg;

// Определяем входы датчиков тормозов и кнопок готовности;
const int brakePin[] = {7,14,17};
const int buttonPin[] = {6,9,16};

// Определяем выходы светодиодов;
const int ledPin[] = {5,8,15};

// Определяем временные периоды. "L" обязательна для типа длинного целого
const unsigned long postingInterval = 10L * 1000L;   // максимальное время между пакетами состояния при отсутствии событий
const unsigned long reconnectInterval = 60L * 1000L; // время без соединения до перезагрузки сети
const unsigned long debounceInterval = 500L;         // Время задержки от дребезга контактов
 
// Определяем переменные
unsigned long lastConnectionTime = 0;             // время последнего удачного сеагса связи
unsigned long previousMillis[] = {0,0,0};         // времена для борьбы с дребезгом контактов датчиков тормоза
byte lastbrakestate[] = {2,2,2};                  // последние состояния датчиков тормоза (заведомо нереальные, чтобы в первом же цикле они приняли реальные значения)
volatile byte linestate[] = {0,0,0};              // статусы линий (0 - не готова, Bxx1 - тормоз Ок, Bx1x - готовность старта(последняя карточка принята), B111 - полная готовность к приему  
volatile bool needSend = false;                   // признак необходимости передачи на старт изменения состояния
volatile unsigned long lastUIN = 0;               // UIN последней считаннаой карточки, 0 если нет необработаных карт

void setup() {
// Определяем номера и параметры входов  
  for (int i=0; i <= 2; i++){
    pinMode(brakePin[i], INPUT_PULLUP);
    pinMode(buttonPin[i], INPUT_PULLUP);
    pinMode(ledPin[i], OUTPUT);
  } 
  
//############## Отладка #############
  Serial.begin(9600);
//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ 

// ждем пока загрузится ethernet-шильд
  delay(1000);
  
// Инициализация Ethernet с фиксированным IP адресом
  Ethernet.begin(macaddr, myip);
  
//############## Отладка #############
  Serial.print("My IP address: ");
  Serial.println(Ethernet.localIP());
//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ 

// Инициализация обработчика протокола Wiegant  
  wg.begin();
  
}

// Основной цикл
void loop() {
  
// Проверка датчики тормозов
  for (int i=0; i <= 2; i++){

    // Проверим изменилось ли состояние   
    if (lastbrakestate[i] != digitalRead(brakePin[i]))  {
      if (previousMillis[i]==0) {                                     // 
        previousMillis[i] = millis();                                 // запомним время этого события    
      } else if (previousMillis[i]+debounceInterval < millis()) {     // прошло ли время дребезга контактов
         lastbrakestate[i] = digitalRead(brakePin[i]);                // сохраним новое состояние
           previousMillis[i]=0;                                       // и обнулим время
        if (lastbrakestate[i] == LOW){                                // изменим статус линии
           linestate[i] = linestate[i] | B001;
         } else {
           linestate[i] = linestate[i] & B010;
           digitalWrite(ledPin[i], LOW);                              // потушим светодиод состояния
         }
        needSend = true;                                              // установим признак изменения состояния

//############## Отладка #############
    Serial.print(i);        
    Serial.print(" brake status = ");        
    Serial.println(lastbrakestate[i]);    
//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^         
       }
     
    } else if (previousMillis[i]) {                                   // это был дребезга контактов
      previousMillis[i]=0;                                            // обнулим время
    } 
    
    if (linestate[i] == B001) {                                       // тормоз в порядке
      // Включим медленное мигание    
      if ((millis()% 2000) > 1000) {digitalWrite(ledPin[i], HIGH);
      } else {digitalWrite(ledPin[i], LOW);}  
    } else if (linestate[i] == B011) {                                // нет неразблокированных карт на старте
      // Проверим состояние кнопок    
      if (digitalRead(buttonPin[i]) == HIGH) {                        // нажата кнопка на финише   
        linestate[i] = B111;
        needSend = true; 
      }
      // Включим ускоренное мигание    
      if ((millis()% 600) > 300) {digitalWrite(ledPin[i], HIGH);
      } else {digitalWrite(ledPin[i], LOW);}        
    } else if (linestate[i] == B111) {                                // полная готовность
      digitalWrite(ledPin[i], HIGH);
    }



  }   // end for...

// Считывание карточки, если она есть
  if (wg.available())
  {
    lastUIN = wg.getCode();
    needSend = true; 
//############## Отладка #############
//    Serial.print("UIN = ");
//    Serial.print(lastUIN,HEX);
//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^    
  }
  
  // Если изменился статус или пришло время пинга отправляем пакет
  if (needSend ||(millis() - lastConnectionTime > postingInterval)) sendState();
   
  // Ждем ответ
  if (ethclient.available()) {
    byte b = ethclient.read();
    if ((b&0xA8)==0xA8){
       for (int i=0; i <= 2; i++){
          if ((b>>i)&1) {
            linestate[2-i] = linestate[2-i] | B10; 
          }
       }
    } else if (b==0xCB){
      lastUIN = 0;
    }
  }
}

// процедура соединения с контроллером на старте
void connectToSrart() {
  // закрываем прошлое соединение если оно было
  ethclient.stop();

  // попыска соединения по адресу serverip на порт 4020
  if (ethclient.connect(serverip, 4020)) {

//############## Отладка #############
Serial.println("connecting...");
//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^    

    // здесь бы неплохо сделать отправку уникального пакета для примитивной авторизации
//    ethclient.println("ЗeC!");

    // note the time that the connection was made:
    lastConnectionTime = millis();
  } else {
    // if you couldn't make a connection:

//############## Отладка #############
    Serial.println("connection failed"); 
//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^    

}
}

// процидура отправки 4х байт состояния или номера последней карточки
void sendState() {

//  client.flush();
  // если активно соединение
  if (ethclient.connected()) {
    byte dat[5] = {0,0,0,0,0};                    // готовим масив
    if (lastUIN>0) {                              // если есть номер карточки записываем в масив в обратном порядке
      for (byte i = 0; i <3; i = i + 1){ 
        dat[3-i]=(lastUIN >> (i*8)) & 0xFF;
      } 
      dat[0]=0xBC;                               // признак того, что пердаются байты состояния
    } else {
      for (byte i = 1; i <=3; i = i + 1){ 
        dat[i]=linestate[i-1];                    // если нет карточки передаем байт состояния
      }  
      dat[0]=0xBD;                              // признак того, что пердаются байты состояния
    }   
    dat[4]=0xEF;                                // стоп байт. в идеале надо бы CRC
    ethclient.write(dat,5);
    needSend = false;

    // сохраним время последнего удачного сеанса
    lastConnectionTime = millis();
  } else {
    // иначе попытка соединиться  
 

//############## Отладка #############
    Serial.println("Trying Reconnect...");
//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^    

    connectToSrart();
  }
}
