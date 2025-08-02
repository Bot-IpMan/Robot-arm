int mq9_analog = A0;
void setup() { Serial.begin(9600); }
void loop() {
  int val = analogRead(mq9_analog);
  Serial.println(val);
  delay(500);
}
