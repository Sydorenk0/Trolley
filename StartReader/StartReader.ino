#include <Wire.h>
#include <Wiegand.h>

const unsigned long debounceInterval = 500L;         //Время защиты от дребезга контактов
const unsigned long WaitInterval = 5000L;            //Время ожидания кода карточки
const unsigned long prestartInterval = 15000L;       //Время ожидания нажатия кнопки старт
const unsigned long startInterval = 5000L;           //Время открытия электромагнитного замка


WIEGAND wg;
volatile byte cmd = 0;
volatile unsigned long lastUIN = 0x0;
volatile byte state = 0;

int laststate = LOW;
unsigned long prevStateMillis = 0;                  //Время изменнения состояния геркона для защиты от дребезга контактов
unsigned long prevWaitMillis = 0;                   //Время начала ожидания кода карточки
unsigned long prestartMillis = 0;                   //Время начала ожидания команды на открытие электромагнитного замка
unsigned long startMillis = 0;
byte myaddr = 94;                                  // I2C Адрес контролера считывателя по умолчанию


void setup() {
// Определяем номера и параметры входов и выходов 
  pinMode(4, OUTPUT);                               // реле электромеханического замка
  pinMode(5, OUTPUT);                               // реле электромагнитного замка   
  pinMode(6, INPUT_PULLUP);                         // вход геркона калитки
  pinMode(7, INPUT_PULLUP);                         // вход 1 для указания I2C Адрес контролера 
  pinMode(8, INPUT_PULLUP);                         // вход 2 для указания I2C Адрес контролера   
// блокируем оба замка  
  digitalWrite(4,HIGH);
  digitalWrite(5,HIGH);
  laststate = digitalRead(6);
  myaddr = myaddr-byte(digitalRead(7))-byte(digitalRead(8))*2;   // устанавливаем I2C Адрес контролера от 101 до 104
  
  wg.begin();                                       // Инициализация обработчика протокола Wiegant  
  
  //############## Отладка #############
  Serial.begin(9600);
    delay(500);
  Serial.println(myaddr);
//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ 
  
  Wire.begin(myaddr);                                 // Инициализация шины i2c с адресом #addr
  Wire.onReceive(receiveEvent);                       // определяем обработчик приема данных по шине i2c
  Wire.onRequest(requestEvent);                       // определяем обработчик запроса данных по шине i2c 
}

void loop() {
// =================== Проверка геркона калитки  ===================
 
  if (laststate != digitalRead(6))  {
    if (prevStateMillis==0) {
      prevStateMillis = millis();    
    } else if (prevStateMillis+debounceInterval < millis()) {   
       laststate = digitalRead(6);
       prevStateMillis=0;
      if (laststate == HIGH){
         state=3;
         prevWaitMillis =  millis();
       } else {
       digitalWrite (5, HIGH);
       if (state!=3) {state=0;} 
       else {prestartMillis=1;}                     // досрочно завершим твймер открытия электромеханического замка
       }
   
 //############## Отладка #############
  Serial.print("state = ");        
  Serial.println(laststate);          
 //^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ 
  
    }
     
  } else if (prevStateMillis) {
    prevStateMillis=0;
  }
  
// Если после сработки геркона прошел таймаут а карточка не считана   
  if ((state == 3) && (prevWaitMillis+WaitInterval < millis())) {
        state=5;
//        lastUIN = 0xFFFFFF;
        lastUIN = 0x123456;
  
//############## Отладка #############
  Serial.println("state = 5");        
  //^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ 
  
 }
  
// =================== Считывание карточки, если она есть ===========================
 
  if(wg.available())
  {
    lastUIN =wg.getCode();
  
//############## Отладка #############
    Serial.print("UIN=");  
    Serial.print(lastUIN,HEX);      
    Serial.println(" State = 4");        
 //^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ 
  
    state=4;                                      // повышаем статус
  }

// =================== обработка команд ===================

//     lastUIN = 0;
  if ((state == 0) && (cmd == 1)) {               // получена команда открытия электромеханического замка
    digitalWrite (4, LOW);                       // открывам электромеханический замок
    prestartMillis = millis();                    // начинаем отсчет времени
    state=1;                                      // повышаем статус
  }
  if ((state == 1) && (cmd == 2)) {              // получена команда открытия электромагнитного замка
      digitalWrite (5, LOW);                    // открывам электромагнитный замок
      startMillis = millis();                    // начинаем отсчет времени
      state=2;                                   // повышаем статус
    } 
  if ((prestartMillis>0) && (prestartMillis+prestartInterval < millis()) && (laststate == LOW)) {   // вышло время открытия электромеханического замка и КАЛИТКА УЖЕ ЗАКРЫТА
      digitalWrite (4, HIGH);                      // закрывам электромеханический замок
      prestartMillis=0;
      if (state!=3) {state=0;}                                    // понижаем статус
    }  
   if ((startMillis>0) &&  (startMillis+startInterval < millis())) {    // вышло время открытия электромагнитного замка
      digitalWrite (5, HIGH);                      // закрывам электромагнитный замок
      startMillis=0;
      if (state==2) {state=1;}                    // понижаем статус
    }
  if (cmd == 9) {                                 // сброс на начальное состояние
    lastUIN=0;
    state=0;
    cmd=0; 
              
  }
}

// =================== Коммуникация для I2C ===========================

// фунция отправляет даные мастеру в ответ на запрос Wire.requestFrom()
// вызывается по прерыванию указанному в setup()
void requestEvent() {
  byte dat[4] = {0,0,0,state};                      // готовим масив
  if (lastUIN>0) {                              // если есть номер карточки записываем в масив в обратном порядке
    for (byte i = 0; i <3; i = i + 1){ 
      dat[2-i]=(lastUIN >> (i*8)) & 0xFF;
      } 
  } 
  Wire.write(dat,4);                            // отправляем масив по I2C
}

// фунция принимает команды от мастера 
// вызывается по прерыванию указанному в setup()
void receiveEvent(int bytes) {
  while (Wire.available()) {                    // пока есть данные на прием считываем что пришло.
      cmd = Wire.read();                        // Для простоты если данных больше, игнорируем все кроме последнего байта.
  
//############## Отладка #############
  Serial.print("in_cmd = ");    
  Serial.print(cmd, HEX);        
  Serial.print(" state = ");    
  Serial.println(state, HEX);    }
 //^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ 
  
}
