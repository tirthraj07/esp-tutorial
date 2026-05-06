#!/bin/bash

# This scripts downloads the AWS IoT Core certificates
# Then renames them and saves them in ../aws_iot/main/
# It assumes that the Policy and Thing are already created in AWS IoT Core
# If you don't have them, create it using CDK first (make deploy)


THING_NAME="ESP8684_Node_CDK"
POLICY_NAME="ESP_Dev_Policy_CDK"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CERT_DIR="${SCRIPT_DIR}/../../remote_control/main"


echo "1. Generating Certificates from AWS IoT Core..."
CERT_ARN=$(aws iot create-keys-and-certificate \
    --set-as-active \
    --certificate-pem-outfile "$CERT_DIR/client.crt" \
    --public-key-outfile "$CERT_DIR/client.pub.key" \
    --private-key-outfile "$CERT_DIR/client.key" \
    --query 'certificateArn' --output text)

echo "2. Attaching Policy to Certificate..."
aws iot attach-policy \
    --policy-name $POLICY_NAME \
    --target $CERT_ARN

echo "3. Attaching Certificate to Thing..."
aws iot attach-thing-principal \
    --thing-name $THING_NAME \
    --principal $CERT_ARN

echo "4. Downloading Amazon Root CA..."
curl -s -o "$CERT_DIR/root_ca.pem" https://www.amazontrust.com/repository/AmazonRootCA1.pem

echo "=========================================="
echo "SUCCESS! Certificates saved in $CERT_DIR"
echo "=========================================="
ls -l "$CERT_DIR"