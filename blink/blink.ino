void setup() {
  pinMode(13, OUTPUT); // Налаштувати пін 13 як вихід
}

void loop() {
  digitalWrite(13, HIGH); // Ввімкнути світлодіод
  delay(10000);           // 10 секунд (10000 мс)

  digitalWrite(13, LOW);  // Вимкнути світлодіод
  delay(10000);           // 10 секунд (10000 мс)
}
