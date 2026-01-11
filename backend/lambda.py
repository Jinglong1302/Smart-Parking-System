import json
import boto3
import base64
import time
from decimal import Decimal

# =================================================================
# 1. CONFIGURATION & CLIENT INITIALIZATION
# =================================================================
TABLE_NAME = "ParkingLot"
LOGS_TABLE_NAME = "ParkingLogs"           
BUCKET_NAME = "parking-lot-images-cpc357" 
REGION = "ap-southeast-1"                 
MAX_SPOTS = 30                             

DEBUG_MODE = True  

# Initialize AWS SDK Clients (Global scope to reuse execution context)
s3 = boto3.client('s3', region_name=REGION)
rekognition = boto3.client('rekognition', region_name=REGION)
dynamodb = boto3.resource('dynamodb', region_name=REGION)
cloudwatch = boto3.client('cloudwatch', region_name=REGION)

# Table References
table = dynamodb.Table(TABLE_NAME)
logs_table = dynamodb.Table(LOGS_TABLE_NAME)

# =================================================================
# 2. MAIN HANDLER
# =================================================================
def lambda_handler(event, context):
    """
    Main entry point for API Gateway.
    Orchestrates: Image decoding -> Storage (Debug) -> OCR -> Logic Routing.
    """
    print("Received event:", json.dumps(event))
    
    # --- Step A: Extract Metadata ---
    # Handle potentially different casing in HTTP headers
    headers = event.get('headers', {})
    action = headers.get('x-parking-action', headers.get('X-Parking-Action', 'ENTRY'))
    print(f"ACTION DETECTED: {action}")

    # --- Step B: Decode Image ---
    image_bytes = None
    try:
        if event.get('isBase64Encoded', False):
            image_bytes = base64.b64decode(event['body'])
        else:
            # Fallback for raw binary or non-proxy integration
            image_bytes = base64.b64decode(event['body'])
    except Exception as e:
        print(f"Decoding Error: {e}")
        return api_response(400, "IMAGE_DECODE_ERROR")

    # --- Step C: Debug Storage (Optional) ---
    filename = "error.jpg"
    if DEBUG_MODE and image_bytes:
        filename = f"{action.lower()}_{int(time.time())}.jpg"
        try:
            s3.put_object(
                Bucket=BUCKET_NAME, 
                Key=filename, 
                Body=image_bytes, 
                ContentType='image/jpeg'
            )
        except Exception as e:
            print(f"S3 Upload Error: {e}")

    # --- Step D: OCR Processing (Amazon Rekognition) ---
    detected_text = []
    if image_bytes:
        try:
            rek_response = rekognition.detect_text(Image={'Bytes': image_bytes})
            for item in rek_response['TextDetections']:
                # Filter for high confidence lines of text
                if item['Type'] == 'LINE' and item['Confidence'] > 70:
                    detected_text.append(item['DetectedText'])
        except Exception as e:
            print(f"Rekognition Error: {e}")

    print(f"Plate Read: {detected_text}")
    plate_read = detected_text[0] if len(detected_text) > 0 else "UNKNOWN"

    # --- Step E: Logic Routing ---
    if action == 'ENTRY':
        return handle_entry(plate_read, filename)
    elif action == 'EXIT':
        return handle_exit(plate_read)
    
    return api_response(400, "INVALID_ACTION")

# =================================================================
# 3. CORE LOGIC FUNCTIONS
# =================================================================

def handle_entry(plate, image_key):
    """
    Validates availability, updates state, and logs entry time.
    """
    # 1. Check Current Availability
    response = table.get_item(Key={'lot_id': 'lot1'})
    
    # Initialize table if empty
    if 'Item' not in response:
        table.put_item(Item={'lot_id': 'lot1', 'available_slots': MAX_SPOTS})
        slots = MAX_SPOTS
    else:
        slots = int(response['Item']['available_slots'])

    if slots <= 0:
        return api_response(200, "FULL")

    # 2. Process Valid Entry
    if plate != "UNKNOWN":
        # A. Decrement Available Slots
        new_slots = slots - 1
        table.update_item(
            Key={'lot_id': 'lot1'},
            UpdateExpression="set available_slots = available_slots - :val",
            ExpressionAttributeValues={':val': Decimal(1)}
        )
        
        # B. Create Session Record (Crucial for calculating duration later)
        current_epoch = int(time.time())
        logs_table.put_item(Item={
            'log_id': plate,  # Partition Key
            'timestamp': time.strftime("%Y-%m-%d %H:%M:%S"),
            'entry_epoch': current_epoch,
            'action': 'ENTRY',
            'image_url': f"https://{BUCKET_NAME}.s3.{REGION}.amazonaws.com/{image_key}"
        })

        # C. Push Metrics to CloudWatch
        send_cloudwatch_data("ENTRY", plate, new_slots, 0)
        
        return api_response(200, "OPEN_GATE")
    else:
        return api_response(200, "DENIED_NO_TEXT")


def handle_exit(plate):
    """
    Updates state, calculates duration, and logs exit.
    """
    # 1. Increment Available Slots
    response = table.update_item(
        Key={'lot_id': 'lot1'},
        UpdateExpression="set available_slots = available_slots + :val",
        ExpressionAttributeValues={':val': Decimal(1)},
        ReturnValues="UPDATED_NEW"
    )
    new_slots = int(response['Attributes']['available_slots'])

    # 2. Calculate Parking Duration
    duration_minutes = 0
    if plate != "UNKNOWN":
        try:
            # Fetch the specific car's entry record
            car_record = logs_table.get_item(Key={'log_id': plate})
            if 'Item' in car_record:
                entry_time = int(car_record['Item']['entry_epoch'])
                exit_time = int(time.time())
                
                duration_seconds = exit_time - entry_time
                duration_minutes = int(duration_seconds / 60)
                
                print(f"Car {plate} stayed for {duration_minutes} minutes.")
        except Exception as e:
            print(f"Duration Calculation Failed: {e}")

    # 3. Push Metrics to CloudWatch
    send_cloudwatch_data("EXIT", plate, new_slots, duration_minutes)
    
    return api_response(200, "EXIT_SUCCESS")

# =================================================================
# 4. MONITORING & UTILS
# =================================================================

def send_cloudwatch_data(action, plate, slots_left, duration_mins):
    """
    Pushes custom metrics to CloudWatch for dashboard visualization.
    """
    try:
        metric_data = []

        # Metric: Current Lot Capacity
        metric_data.append({
            'MetricName': 'AvailableSlots_Demo',
            'Value': slots_left,
            'Unit': 'Count'
        })

        if action == 'ENTRY':
            # Metric: Daily Volume Counter
            metric_data.append({
                'MetricName': 'DailyCarCount',
                'Value': 1,
                'Unit': 'Count'
            })
        
        elif action == 'EXIT' and duration_mins > 0:
            # Metric: Duration (Only relevant on exit)
            metric_data.append({
                'MetricName': 'ParkingDuration',
                'Value': duration_mins,
                'Unit': 'Count' # Treated as count of minutes
            })

        cloudwatch.put_metric_data(
            Namespace='SmartParking',
            MetricData=metric_data
        )
        
    except Exception as cw_error:
        print(f"CloudWatch Push Error: {cw_error}")

def api_response(code, message):
    """
    Standardized JSON response format for API Gateway.
    """
    return {
        "statusCode": code,
        "body": message
    }