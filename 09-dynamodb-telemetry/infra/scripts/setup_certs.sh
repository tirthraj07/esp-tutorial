#!/bin/bash

# Default to ESP_NODE_001 if no environment variable is provided
ESP_NODE_ID="${ESP_NODE_ID:-ESP_NODE_001}"

THING_NAME="${ESP_NODE_ID}"
# Hardcoded to the single policy created by CDK
POLICY_NAME="ESP_Fleet_Policy" 

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CERT_DIR="${SCRIPT_DIR}/../../telemetry_task/main"

echo "=========================================="
echo "Provisioning Device: $THING_NAME"
echo "=========================================="

echo "1. Creating Thing in AWS IoT Core..."
aws iot create-thing --thing-name $THING_NAME

echo "2. Generating Certificates..."
CERT_ARN=$(aws iot create-keys-and-certificate \
    --set-as-active \
    --certificate-pem-outfile "$CERT_DIR/client.crt" \
    --public-key-outfile "$CERT_DIR/client.pub.key" \
    --private-key-outfile "$CERT_DIR/client.key" \
    --query 'certificateArn' --output text)

echo "3. Attaching Fleet Policy to Certificate..."
aws iot attach-policy \
    --policy-name $POLICY_NAME \
    --target $CERT_ARN

echo "4. Attaching Certificate to Thing..."
aws iot attach-thing-principal \
    --thing-name $THING_NAME \
    --principal $CERT_ARN

echo "5. Downloading Amazon Root CA..."
curl -s -o "$CERT_DIR/root_ca.pem" https://www.amazontrust.com/repository/AmazonRootCA1.pem

echo "=========================================="
echo "SUCCESS! Device $THING_NAME is ready."
echo "Certificates saved in $CERT_DIR"
echo "=========================================="