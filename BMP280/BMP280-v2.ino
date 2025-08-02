/*
 * РОБОУКА з підтримкою Node-RED керування через TX12
 * Arduino Mega 2560 + PCA9685 + Сервоприводи MG90S
 * Підтримка команд від Node-RED
 */

#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

// Створюємо об'єкт PCA9685
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

// Налаштування сервоприводів MG90S
const int SERVO_MIN = 150;   // ~0.75ms pulse 
const int SERVO_MAX = 600;   // ~3.0ms pulse
const int SERVO_MID = 375;   // ~1.875ms pulse (90°)

// Канали сервоприводів
const int BASE_SERVO = 0;      // S1
const int SHOULDER_SERVO = 1;  // S2
const int ELBOW_SERVO = 2;     // S3
const int WRIST_SERVO = 3;     // S4
const int GRIPPER_SERVO = 4;   // Захоплювач

// Поточні позиції сервоприводів
int current_positions[5] = {90, 90, 90, 90, 90}; // Початкові позиції в градусах

// Змінні для обробки команд
String inputBuffer = "";
bool commandReady = false;

void setup() {
  Serial.begin(115200);
  
  // Очікуємо підключення
  while (!Serial) {
    delay(10);
  }
  
  Serial.println("=== РОБОУКА NODE-RED CONTROL ===");
  
  // Ініціалізація I2C
  Wire.begin();
  
  // Ініціалізація PCA9685
  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(50);  // 50Hz для сервоприводів
  
  // Вимикаємо всі канали
  for (int i = 0; i < 16; i++) {
    pwm.setPWM(i, 0, 0);
  }
  
  delay(500);
  
  // Переміщуємо всі сервоприводи в початкову позицію
  moveAllToHome();
  
  Serial.println("✓ Система готова до роботи");
  Serial.println("Очікую команди від Node-RED...");
}

void loop() {
  // Обробка вхідних даних
  while (Serial.available() > 0) {
    char inChar = (char)Serial.read();
    
    if (inChar == '\n' || inChar == '\r') {
      if (inputBuffer.length() > 0) {
        processCommand(inputBuffer);
        inputBuffer = "";
      }
    } else {
      inputBuffer += inChar;
    }
  }
  
  // Невеликка затримка для стабільності
  delay(10);
}

void processCommand(String command) {
  command.trim();
  
  if (command.length() == 0) return;
  
  Serial.print("Отримано: ");
  Serial.println(command);
  
  // Обробка множинних команд (розділені ;)
  int startIndex = 0;
  int endIndex = command.indexOf(';');
  
  while (endIndex != -1 || startIndex < command.length()) {
    String singleCommand;
    
    if (endIndex != -1) {
      singleCommand = command.substring(startIndex, endIndex);
      startIndex = endIndex + 1;
      endIndex = command.indexOf(';', startIndex);
    } else {
      singleCommand = command.substring(startIndex);
      startIndex = command.length();
    }
    
    singleCommand.trim();
    if (singleCommand.length() > 0) {
      executeSingleCommand(singleCommand);
    }
  }
}

void executeSingleCommand(String cmd) {
  cmd.toUpperCase();
  
  // Команди керування сервоприводами S1-S4
  if (cmd.startsWith("S1:")) {
    int angle = cmd.substring(3).toInt();
    moveServo(BASE_SERVO, angle);
  }
  else if (cmd.startsWith("S2:")) {
    int angle = cmd.substring(3).toInt();
    moveServo(SHOULDER_SERVO, angle);
  }
  else if (cmd.startsWith("S3:")) {
    int angle = cmd.substring(3).toInt();
    moveServo(ELBOW_SERVO, angle);
  }
  else if (cmd.startsWith("S4:")) {
    int angle = cmd.substring(3).toInt();
    moveServo(WRIST_SERVO, angle);
  }
  
  // Команди захоплювача
  else if (cmd == "GRIP:OPEN") {
    moveServo(GRIPPER_SERVO, 0);    // Відкрити
    Serial.println("✓ Захоплювач відкрито");
  }
  else if (cmd == "GRIP:CLOSE") {
    moveServo(GRIPPER_SERVO, 180);  // Закрити
    Serial.println("✓ Захоплювач закрито");
  }
  
  // Спеціальні команди
  else if (cmd == "HOME") {
    moveAllToHome();
    Serial.println("✓ Повернення в домашню позицію");
  }
  else if (cmd == "READY") {
    moveAllToReady();
    Serial.println("✓ Готова позиція");
  }
  else if (cmd == "STOP") {
    stopAllServos();
    Serial.println("✓ Всі сервоприводи зупинено");
  }
  else if (cmd == "STATUS") {
    printStatus();
  }
  
  // Діагностичні команди
  else if (cmd == "TEST") {
    testAllServos();
  }
  else if (cmd == "PING") {
    Serial.println("PONG");
  }
  
  else {
    Serial.print("❌ Невідома команда: ");
    Serial.println(cmd);
  }
}

void moveServo(int servo_num, int angle) {
  // Обмежуємо кут
  angle = constrain(angle, 0, 180);
  
  // Перетворюємо кут в PWM значення
  int pwm_value = map(angle, 0, 180, SERVO_MIN, SERVO_MAX);
  
  // Надсилаємо команду
  pwm.setPWM(servo_num, 0, pwm_value);
  
  // Зберігаємо поточну позицію
  if (servo_num < 5) {
    current_positions[servo_num] = angle;
  }
  
  Serial.print("✓ Сервопривод ");
  Serial.print(servo_num);
  Serial.print(" → ");
  Serial.print(angle);
  Serial.println("°");
}

void moveAllToHome() {
  // Домашня позиція - всі сервоприводи в середині
  for (int i = 0; i < 5; i++) {
    moveServo(i, 90);
    delay(100);  // Невелика затримка між рухами
  }
}

void moveAllToReady() {
  // Готова позиція для роботи
  moveServo(BASE_SERVO, 90);      // База по центру
  moveServo(SHOULDER_SERVO, 45);  // Плече піднято
  moveServo(ELBOW_SERVO, 135);    // Лікоть зігнуто
  moveServo(WRIST_SERVO, 90);     // Зап'ястя прямо
  moveServo(GRIPPER_SERVO, 45);   // Захоплювач напіввідкритий
  delay(1000);
}

void stopAllServos() {
  // Вимикаємо всі сервоприводи
  for (int i = 0; i < 5; i++) {
    pwm.setPWM(i, 0, 0);
  }
}

void testAllServos() {
  Serial.println("🔄 Тест всіх сервоприводів...");
  
  for (int servo = 0; servo < 5; servo++) {
    Serial.print("Тестуємо сервопривод ");
    Serial.println(servo);
    
    // Тест трьох позицій
    moveServo(servo, 0);
    delay(500);
    moveServo(servo, 90);
    delay(500);
    moveServo(servo, 180);
    delay(500);
    moveServo(servo, 90);
    delay(500);
  }
  
  Serial.println("✓ Тест завершено");
}

void printStatus() {
  Serial.println("=== СТАТУС РОБОРУКИ ===");
  Serial.print("База (S1): ");
  Serial.println(current_positions[0]);
  Serial.print("Плече (S2): ");
  Serial.println(current_positions[1]);
  Serial.print("Лікоть (S3): ");
  Serial.println(current_positions[2]);
  Serial.print("Зап'ястя (S4): ");
  Serial.println(current_positions[3]);
  Serial.print("Захоплювач: ");
  Serial.println(current_positions[4]);
  Serial.println("=====================");
}

// Функція для плавного руху (опціонально)
void smoothMove(int servo_num, int target_angle) {
  int current_angle = current_positions[servo_num];
  int step = (target_angle > current_angle) ? 1 : -1;
  
  for (int angle = current_angle; angle != target_angle; angle += step) {
    moveServo(servo_num, angle);
    delay(20);  // Швидкість плавного руху
  }
}