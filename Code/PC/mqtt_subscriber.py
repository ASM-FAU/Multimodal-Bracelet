import paho.mqtt.client as mqtt
import csv
import struct
import queue
import threading

# MQTT server settings
mqtt_server = "enter IP address"  # localhost

mqtt_port = 1883
mqtt_topic = "fmg_data"

# Global variables used to store the file object and message queue
csvfile = None
message_queue = queue.Queue()

# Function for the file writing thread
def file_writer_thread():
    global csvfile
    while True:
        item = message_queue.get()  # Get an item from the queue
        if item is None:  # None is used as a stop signal
            break
        if csvfile:  # Ensure the file is open
            csvwriter = csv.writer(csvfile)
            csvwriter.writerow(item)
        message_queue.task_done()

# Callback function when connected to the server
def on_connect(client, userdata, flags, rc):
    global csvfile
    print("Connected with result code " + str(rc))
    client.subscribe(mqtt_topic)

    # Create and open the CSV file on the first connection
    csvfile = open('mqtt_data.csv', 'a', newline='')
    csvwriter = csv.writer(csvfile)
    header = ['FSR{:02d}'.format(i) for i in range(1, 25)] + ['Timestamp']
    csvwriter.writerow(header)

# Callback function when a message is received from the subscribed topic
def on_message(client, userdata, msg):
    print(f"Topic: {msg.topic} Message received")

    # Parse the binary message
    data = list(struct.unpack('<24fQ', msg.payload))
    # Put the parsed data into the queue
    message_queue.put(data)

# Create an MQTT client instance
client = mqtt.Client(clean_session=True)
client.on_connect = on_connect
client.on_message = on_message

# Connect to the MQTT server
client.connect(mqtt_server, mqtt_port, 60)

# Start the file writing thread
thread = threading.Thread(target=file_writer_thread, daemon=True)
thread.start()

try:
    client.loop_forever()
except KeyboardInterrupt:
    # Capture Ctrl+C, send a stop signal to the thread, and wait for it to finish
    message_queue.put(None)
    thread.join()
    if csvfile:
        csvfile.close()
    print("\nProgram has exited")