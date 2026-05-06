```bash
cd infra
make setup
make deploy
cd ../remote_control
idf.py set-target esp32c2
idf.py build flash monitor
```

Wait for the terminal to `print >>> SUCCESS! AWS IoT Core mTLS Handshake Complete! <<<.`  

Open your AWS Console web browser.  

Go to the MQTT test client.  

Publish to the topic `esp8684/commands`.  

Send this exact JSON payload:  

```json
{
  "command": "ON"
}
```
If everything is wired up correctly, you will hit the publish button in your browser in AWS US-East, and less than 100 milliseconds later, the blue LED on your desk in Pune will snap on. Send `{"command": "OFF"}` to kill it.