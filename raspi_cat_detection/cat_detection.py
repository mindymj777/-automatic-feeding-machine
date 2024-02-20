from flask import Flask, render_template, Response


import argparse
import sys
import time
import cv2
from tflite_support.task import core
from tflite_support.task import processor
from tflite_support.task import vision
import utils
import RPi.GPIO as GPIO


servo_pin = 24
GPIO.setmode(GPIO.BCM)
GPIO.setup(servo_pin, GPIO.OUT)
pwm = GPIO.PWM(servo_pin, 50)
pwm.start(0)

def set_angle(angle):
  duty_cycle = (angle / 18) + 2
  pwm.ChangeDutyCycle(duty_cycle)
  time.sleep(1.5)
  
model='efficientdet_lite0.tflite'
camera_id=0
width=1920
height= 1080
num_threads= 4
enable_edgetpu= False


global cat_detected
cat_detected = False

start_time = time.time()

cap = cv2.VideoCapture(camera_id)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)

row_size, left_margin = 20, 24
text_color = (0, 0, 255)
font_size, font_thickness = 1, 1


base_options = core.BaseOptions(file_name=model, use_coral=enable_edgetpu, num_threads=num_threads)
detection_options = processor.DetectionOptions(max_results=3, score_threshold=0.6)
options = vision.ObjectDetectorOptions(base_options=base_options, detection_options=detection_options)
detector = vision.ObjectDetector.create_from_options(options)


cap = cv2.VideoCapture(0)

app = Flask(__name__)

def utils(image,detection_result):
    
    for detection in detection_result.detections:
        # Draw bounding_box
        bbox = detection.bounding_box
        start_point = bbox.origin_x, bbox.origin_y
        end_point = bbox.origin_x + bbox.width, bbox.origin_y + bbox.height
        cv2.rectangle(image, start_point, end_point, (0, 0, 255), 3)
        
        # Draw label and score
        category = detection.categories[0]
        category_name = category.category_name
        probability = round(category.score, 2)
        result_text = category_name + ' (' + str(probability) + ')'
        text_location = (10 + bbox.origin_x,10+ 10 + bbox.origin_y)
        text_location = (10 + bbox.origin_x,10+ 10 + bbox.origin_y)
        cv2.putText(image, result_text, text_location,cv2.FONT_HERSHEY_SIMPLEX,1,(0, 0, 255))

                
    return image

def detectcat(detection_result):
    global cat_detected
    for detection in detection_result.detections:
       
        category = detection.categories[0]
        category_name = category.category_name
        probability = round(category.score, 2)
        
        if category_name.lower()=='cat' and probability >0.3:
            cat_detected=True
        else:
            cat_detected=False
        print(cat_detected)
            
def update_detect_result(detection_result):
    
    success, image = cap.read()
    if not success:
            sys.exit('ERROR: Unable to read from webcam. Please verify your webcam settings.')

    image = cv2.flip(image, 1)
    rgb_image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
    input_tensor = vision.TensorImage.create_from_array(rgb_image)
    detection_result = detector.detect(input_tensor)
    return detection_result
    
    
def update_image(image):
    success, image = cap.read()
    if not success:
            sys.exit('ERROR: Unable to read from webcam. Please verify your webcam settings.')

    image = cv2.flip(image, 1)
    rgb_image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
    input_tensor = vision.TensorImage.create_from_array(rgb_image)
    detection_result = detector.detect(input_tensor)
    image=utils(image,detection_result)
    
    return image


def gen_frames():
    num=0    
    global cap

    cap = cv2.VideoCapture(0)

    while cap.isOpened():
		
        success, image = cap.read()

        if not success:
            sys.exit('ERROR: Unable to read from webcam. Please verify your webcam settings.')


        image = cv2.flip(image, 1)

        rgb_image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
        input_tensor = vision.TensorImage.create_from_array(rgb_image)
        detection_result = detector.detect(input_tensor)
        
        image= utils(image, detection_result)
        
        detectcat(detection_result)
    
        if(num > 100):
            print("start detect!")
            if not cat_detected:
                pwm.start(0)
                for angle in range(0, 180, 10):
                    time.sleep(1)
                    set_angle(angle)
                    image=update_image(image)
                    detection_result=update_detect_result(detection_result)  
                    detectcat(detection_result)
                
                    if(cat_detected):
                        print("Cat detected! Stopping motor.")
                        pwm.ChangeDutyCycle(0)
                        ret, buffer = cv2.imencode('.jpg', image)
                        image = buffer.tobytes()
                        yield (b'--frame\r\n'
                           b'Content-Type: image/jpeg\r\n\r\n' + image + b'\r\n')
                        break
                    
                    ret, buffer = cv2.imencode('.jpg', image)
                    image = buffer.tobytes()
                    yield (b'--frame\r\n'
                           b'Content-Type: image/jpeg\r\n\r\n' + image + b'\r\n')
                           
                    
        num+=1
        image=update_image(image)
        detection_result=update_detect_result(detection_result)  
       
        ret, buffer = cv2.imencode('.jpg', image)
        image = buffer.tobytes()
        yield (b'--frame\r\n'
               b'Content-Type: image/jpeg\r\n\r\n' + image + b'\r\n')

@app.route('/video_feed')
def video_feed():
    return Response(gen_frames(),
                    mimetype='multipart/x-mixed-replace; boundary=frame')

@app.route('/')
def index():
    return render_template('index.html')

if __name__ == '__main__':
    app.run('0.0.0.0')

