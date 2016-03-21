
#include <Wire.h>                     // библиотека для работы с шиной i2c 
#include <Ethernet.h>                 // библиотека для работы с шиной i2c 
#include <ds3231.h>                   // библиотека для работы с модулем RTC 
#include <I2C_eeprom.h>               // библиотека для работы с EEPROM через шиной i2c 

#define EEPROMSIZE 4096              // размер EEPROM подключеной к i2c

// ====================================================
//     Сетевые настройки контроллера
//   хорошо бы хранить в EEPROM контроллера или i2c 
//   с возможностью изменения без перепрограмирования 
// ====================================================

byte mymacaddr[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xE1 }; //  MAC адрес устройства (должен быть уникален в сети

IPAddress myip(192, 168, 1, 235);                          // IP адрес этого устройства
byte mygateway[] = { 192, 168, 1, 1 };                     // адрес шлюза 
byte mynetmask[] = { 255, 255, 255, 0 };                   // маска подсети: 

char webserveraddr[] = "193.178.187.50";                    // IP адрес интернет-сервера учета
int  webserverport = 80;//81;                                   // порт интернет-сервера учета
char webpagename[] = "/eye/public/index.php/index/transaction/index.php";                   // 
int  mytoken = 123;                                        //

// ====================================================
//     Глобальные перемернные
// ====================================================

EthernetServer myserver = EthernetServer(4020);           // создаем объект сервера на порту 4020 (мощет быть любой свободный)

EthernetClient webclient;                                 //  

I2C_eeprom eeprom(0x57, EEPROMSIZE);                      // содаем объект для работы по шине i2c с EEPROM по адресу 0x57


//volatile unsigned long lastUIN[] = {0ul,0x17BDDDul,0xD6B995ul};
volatile unsigned long lastUIN[] = {0L,0L,0L};            // для кодов карточек считаных при старте
volatile byte linestate[] = {0,0,0};                      // для состояния линий 
                                                          // описание битов от старшего к младшему
                                                          // 0 не используется
                                                          // 0 не используется
                                                          // 0/1 небыло проходов / был считан код карточки на старте или открывалась калитка но код не считан (тогда код FFFFFF)
                                                          // 0/1 заблокирован / разблокирован эл.магнитный замок
                                                          // 0/1 заблокирован / разблокирован эл.мезанический замок
                                                          // 0/1 нет готовности / была нажата кнопка готовности линии на финише
                                                          // 0/1 есть/нет заблокированых кодов на старте (нет неснятых подвесов/людей на линии) 
                                                          // 0/1 тормоз на финише неготов/взведен

volatile uint16_t headaddr = 0;
volatile uint16_t tailaddr = 0;

byte buff[] = {0,0,0,0,0,0};
byte recv = 0;
byte cmd = 0;
unsigned long lastping = 0;


// ====================================================
//     Процедура инициализации
// ====================================================

void setup() {
  pinMode(5, INPUT_PULLUP);                             // вход кнопки говнсти
  pinMode(6, INPUT_PULLUP);                             // вход кнопки открытия дверей
//###########################  
  pinMode(7, INPUT_PULLUP);                             // вход сервисной кнопки 
//##########################  
  Wire.begin();                                         // Инициализация шины i2c 
 
//###### О Т Л А Д К А ######  
 Serial.begin(9600);                                   
//##########################  

  eeprom.begin();                                       // Инициализация EEPROM на шине i2c 
  
  DS3231_init(DS3231_INTCN);                            // Инициализация модуля RTC  
    
// сброс настроек если кнопка зажата при включении
  if (digitalRead(7)==LOW) {                         
    setdefault();                                      
   }
  
// ждем пока загрузится ethernet-шильд
  delay(1000);

  headaddr = eeprom.readByte(0)*0xFF+eeprom.readByte(1);   // зчитывем значение указателяна на начало FIFO хранилища
  tailaddr = eeprom.readByte(2)*0xFF+eeprom.readByte(3);   // зчитывем значение указателяна на хвост FIFO хранилища
    
  Ethernet.begin(mymacaddr, myip);                         // Инициализация Ethernet с фиксированным IP адресом
 
  myserver.begin();                                        // запуск Ethernet-сервера для связи финишем 
    
//############## Отладка #############
    Serial.println(" Controller started");     

    struct ts t;
    char buff[60];
    DS3231_get(&t);        
    snprintf(buff, 60, "%d.%02d.%02d %02d:%02d:%02d %lu", t.year,
             t.mon, t.mday, t.hour, t.min, t.sec, t.unixtime);
   Serial.println(buff);
  Serial.print("headeaddr ");         // print the character    
  Serial.println(headaddr,DEC);         // print the character    
  Serial.print("tailaddr ");         // print the character    
  Serial.println(tailaddr,DEC);         // print the character    

  sendtoserver(2u,0x123456lu,t.unixtime);     
 //^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ 
 
}  // end setup()
  
// ====================================================
//               Основной цикл
// ====================================================
void loop() {

  if (digitalRead(5)==LOW) {                            // Проверка нажатия кнопки подключенной к 5 входу контроллера
    for (byte i = 0; i <=2; i++){                       // хорошо бы еще добавить защиту от дребезга контактов, как на финише например
      if (linestate[i]==B00000111) {                    // если есь готовнось линии на финише
         sendcommand(i, 1);                             // Отправка комманды открытия эл.механических замков      
      }
    }     
   
//############## Отладка #############
    Serial.println("Unsecure doors");        
 //^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ 
 
    delay(100);                                         // необязательно
    
  } else if (digitalRead(6)==LOW) {                     // Проверка нажатия кнопки подключенной к 5 входу контроллера
    for (byte i = 0; i <=2; i++){                       // хорошо бы еще добавить защиту от дребезга контактов, как на финише например
      if (linestate[i]==B00001111) {                    // если эл.механический замок уже разблокирован
         sendcommand(i, 2);                             // Отправка комманды открытия эл.мегнитных замков           
      }
    }     

    
//############## Отладка #############
    Serial.println("Open doors");        
 //^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ 
 
     delay(100);
  } else if (lastping+1000<millis()){                    // Раз в 1 секунду (можно изменить)        
    for (byte i = 0; i <=2; i++){ 
      getreaderstate(i);                                 // опрос состояния всез контроллеров калиток      
      if (lastUIN[i]>10) {                               // если принят код карты      
         sendcommand(i, 9);                              // отправляем команду подтверждения приема      
         if (linestate[i] & B00100000) {savelogrec(i);}   // если проход (открытие калитки) зафиксирован - пишем в журнал      
      }
    }
    lastping=millis();    
  }



//###########################  
// для отладки принудительно сбрасывал код карточки замыканием 7го входа на землю.
// в жизни возможно имеет смысл добавить для искл. ситуаций (потери свяли с финишем и.т.д) с записью в журнал событий

  if (digitalRead(7)==LOW) {
    for (byte i = 0; i <=2; i++){ 
      lastUIN[i]=0;
 // для отладки     vieweeprom();
    }     
  }
//###########################  

  
  //========= обработка сетевого соединения с финишем =======================
  // при подсоединении клиента появляются непрочитанные байты, доступные для чтения:
  EthernetClient client = myserver.available();
  if (client) {
//############## Отладка #############
    Serial.println("new client");
//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ 
    while (client.connected()) {                                        // ожидаем подключение
      if (client.available()) {                                         // если есть данные
        byte c = client.read();                                         // считываем
        if (recv>0) {
          if (recv<4) {
            buff[recv]=c;                                               // сохраняем в буфере
            recv++;
          } else{
            if (c==0xEF){                                               // признак конца пакета
              if ( buff[0] == 0xBC) {                                   // если 1 байт 0xBC - значит в пакете код карточки
                unsigned long UIN=0;
                for (byte i = 1; i <=3; i++){                          // сохраним его в виде unsigned long
                  UIN = (UIN << 8) + buff[i];
                  for (byte i = 0; i <=2; i++){ 
                    if (UIN == lastUIN[i]) { lastUIN[i]=0;}  
                  }
                }
                myserver.write(0xCB);                                   // подтвердим прием еода сарты
              } else if ( buff[0] == 0xBD) {
                for (byte i = 0; i <=2; i++){                          // сохраним его в виде unsigned long
                linestate[i] = (linestate[i] & B11111000) | (buff[i+1] & B00000111);        // установим биты состояния линии на финише
                }
                
              }
                c = 0xA8;                                               // подготовим байт ответа 10101000 - для красоты :)
                for (byte i = 0; i <=2; i++){ 
                  if (lastUIN[2-i] == 0 ) {                             // если нет зарегистрированных карточек  
                    c = c | (1<<i);                                     // установим бит готовности старта  
                  }
                }

                myserver.write(c);                                      // и передадим на финиш  
            } 
            recv=0;
          }
          
        } else if ((c == 0xBC) || (c == 0xBD)){
          buff[0]=c;
          recv=1;
        }
//############## Отладка #############
        Serial.print(c,HEX);
//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ 

      }   //end data avalible
    }     //end while connected
 
//############## Отладка #############
    Serial.println("disconnected"); 
//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ 
  }   // конец приема данных с финиша
  
}     // end loop()

// ====================================================
//     Процедура управления считывателем / воротами
// ====================================================

void sendcommand (byte addr, byte cmd)

{
  Wire.beginTransmission(91+addr);   // начинаем процедуру передачи устройству с адресом #100+номер контроллера)
  Wire.write(cmd);                   // отправляем байт данных
  Wire.endTransmission();            // завершаем процедуру передачи
}
 
// ====================================================
//        Процедура опроса считывателя / ворот
// ====================================================

void getreaderstate(byte addr)
{
  unsigned long lastdata = 0;
  Wire.requestFrom(91+addr, 4);               // запрос 4 байт состояния считывателя с i2c адресом 91+addr
  while (Wire.available()) {                  // пока еть даные 
    lastdata = (lastdata << 8) + Wire.read(); // считываем даные ответа
  }
  if (lastdata>10) {                          // если есть код карточки
    lastUIN[addr]=lastdata>>8;                // считываем его игнорируя байт состояния
  } 
        
  linestate[addr] = (linestate[addr] & B00000111) | (lastdata & B11111000);    // считываем байт состояния 
  
//############## Отладка #############
  Serial.print("Reader ");   
  Serial.print(addr,DEC);
  Serial.print(" State=");  
  Serial.print(linestate[addr],BIN);  
  Serial.print(" UIN="); 
  Serial.println(lastUIN[addr],HEX);
//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ 

}

// ====================================================
//        Функция записи события на сервер
// ====================================================

byte sendtoserver(byte readerid, unsigned long cardcode, unsigned long unixtime)
{
  int inChar;
  char outBuf[64];
  char params[64];
    
//  Serial.print("connecting...");

  if(webclient.connect(webserveraddr,webserverport) == 1)
  {
//    Serial.println("connected");
    
// ====================================================
//   пришлось добавить так как вывод 32-битных целых в HEX через sprintf() 
//   работает некоректно (выводит только 2 младших байта)
    char *str = &params[sizeof(params) - 1];

    *str = '\0';
    do {
      unsigned long m = cardcode;
      cardcode /= 16;
      char c = m - 16 * cardcode;
      *--str = c < 10 ? c + '0' : c + 'A' - 10;
    } while(cardcode);
    
// в итоге получим текстовое шеснадцатиричное значение кода карточки
// Правильнее бы еще добавить приведение к стандартной длинне - 6 символов (максимум для 24 значащих бит Wiegant26) 
// нулями спереди. Хотя можно и на стороне сервера   
// ====================================================

// Подготовка данных. !!!!! Строку надо привести в соответсвие с моделью на стороне сервера !!!!!

//    sprintf(params,"token=%d&cardid=\"%s\"&readerid=%d&date=%lu",mytoken,str,readerid,unixtime);     // для ontent-Type: application/x-www-form-urlencoded
    sprintf(params,"{\"token\":\"%d\",\"cardId\":\"%s\",\"readerid\":\"%d\",\"date\":\"%lu\"}",mytoken,str,readerid,unixtime);    // Для Content-Type: application/json

// =========== подготовим и отправим header  ===============
    sprintf(outBuf,"POST %s HTTP/1.1",webpagename);
    webclient.println(outBuf);
    sprintf(outBuf,"Host: %s",webserveraddr);
    webclient.println(outBuf);
//    webclient.println(F("Connection: close\r\nContent-Type: application/x-www-form-urlencoded"));
    webclient.println(F("Connection: close\r\nContent-Type: \"application/json; charset=utf-8\""));        // !!!!! тут надо указать нужный Content-Type !!!!!
    sprintf(outBuf,"Content-Length: %u\r\n",strlen(params));
    webclient.println(outBuf);

    webclient.print(params);                                                                               // отправляем непосредственно данные
  }
  else
  {
//    Serial.println(F("failed"));
    return 0;
  }

// ====================================================
//    попытка получить ответ

  int connectLoop = 0;

  while(webclient.connected())
  {
    while(webclient.available())
    {
      inChar = webclient.read();
// ====================================================
//   здесь должен быть обработчик ответа сервера. В случае удачи -   return 1;     

//############## Отладка #############
      Serial.write(inChar);       
//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^          
      connectLoop = 0;
    }

    delay(1);
    connectLoop++;
    if(connectLoop > 10)
    {
 //     Serial.println();
//      Serial.println(F("Timeout"));
      webclient.stop();
    }
  }

  Serial.println();
  Serial.println(F("disconnecting."));
  webclient.stop();
  return 0;
}


// =========================================================
// Процедура записи в EEPROM при отсутсвии связи с сервером
// =========================================================

void savetoeeprom(byte addr, unsigned long unixtime)
{
  if ((tailaddr<8)||(tailaddr>=EEPROMSIZE)) {return;}             // неправильно считаны значения tailaddr. Не пишем в память чтобы не стереть уже записанные данные
    
  byte eepromBuffer[8];
 
  for (byte i = 0; i <4; i++){                                  // заполняем блок данных: 0й байт - № считывателя, 1й-3й - код карточки, 4й-7й - время в секундах от 1970г. (unix-формат) 
    eepromBuffer[3-i]=(lastUIN[addr] >> (i*8)) & 0xFF;
    eepromBuffer[7-i]=(unixtime >> (i*8)) & 0xFF;
  }
  eepromBuffer[0] = addr; 

  eeprom.writeBlock(tailaddr, eepromBuffer, 8);                 // записываем даннык в хвост FIFO хранилища
  
  tailaddr=tailaddr+8;                                          // изменяем указатель на хвост
  if (tailaddr>=EEPROMSIZE) {tailaddr=8;}                       // проверяем границы указателя на хвост. если необходимо переходим на начало EEPROM
  eeprom.writeByte(2, tailaddr >> 8);                           // сохраняем значение указателя на хвост
  eeprom.writeByte(3, tailaddr & 0xFF);

  if (tailaddr==headaddr) {                                      // если указатель на хвост догнал на круг указатель на начало  FIFO хранилища
    headaddr = tailaddr+8;                                       // изменяем указатель на начало, жертвуя самым старым значением
    if (headaddr>=EEPROMSIZE) {headaddr=8;}                      // проверяем границы указателя на начало. если необходимо переходим на начало EEPROM
    eeprom.writeByte(0, headaddr >> 8);                          // сохраняем значение указателя
    eeprom.writeByte(1, headaddr & 0xFF);    
  }
}

// ====================================================
//        Процедура записи события в журнал
// ====================================================

void savelogrec(byte addr)
{
struct ts t;
  DS3231_get(&t);                                               // получаем время с модуля RTC
 
  if (sendtoserver(addr,lastUIN[addr],t.unixtime) != 1) {       // пытаемся передать сообщение на сервер
    savetoeeprom(addr, t.unixtime);                             // записсываеи в EEPROM при отсутсвии связи с сервером
  }

}  

// ====================================================
// Процедура сброса на заводские установки
//  !!! позхе надо добавить сброс параметров, которые можно настраивать (MAC, IP, адрес сервера, и.т.д.)  
// ====================================================
void setdefault()
{

// обнуляем значения указателей начала и хвоста FIFO хранилища
 eeprom.writeByte(0, 0);              
 eeprom.writeByte(1, 8);
 eeprom.writeByte(2, 0);
 eeprom.writeByte(3, 8);  
}

// =====================================================
//              процедуры для отладки
// ====================================================
void vieweeprom()
{
  byte eepromBuffer[8];
 


 for (byte i = 0; i <=tailaddr; i=i+8){  
   eeprom.readBlock(i, eepromBuffer, 8);
  Serial.print("Addr="); 
  Serial.print(i,DEC); 
  Serial.print(" Data=");  
  for (byte j = 0; j <=7; j++){  
    Serial.print(eepromBuffer[j],HEX);   
    Serial.print(" "); 
  }
  Serial.println("");     
 }

}


