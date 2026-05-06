#!/bin/bash

THING_NAME="ESP8684_Node_CDK"
POLICY_NAME="ESP_Dev_Policy_CDK"

echo "1. Finding the attached Certificate..."
CERT_ARN=$(aws iot list-thing-principals --thing-name $THING_NAME --query 'principals[0]' --output text)

if [ "$CERT_ARN" == "None" ] || [ -z "$CERT_ARN" ]; then
    echo "No certificates attached. You are safe to run cdk destroy!"
    exit 0
fi

# Extract the ID from the ARN (everything after the '/')
CERT_ID=$(echo $CERT_ARN | cut -d'/' -f2)
echo "Found Certificate ID: $CERT_ID"

echo "2. Detaching Certificate from Thing..."
aws iot detach-thing-principal \
    --thing-name $THING_NAME \
    --principal $CERT_ARN

echo "3. Detaching Policy from Certificate..."
aws iot detach-policy \
    --policy-name $POLICY_NAME \
    --target $CERT_ARN

echo "4. Deactivating Certificate..."
aws iot update-certificate \
    --certificate-id $CERT_ID \
    --new-status INACTIVE

echo "5. Deleting Certificate from AWS..."
aws iot delete-certificate \
    --certificate-id $CERT_ID

echo "=========================================="
echo "CLEANUP SUCCESSFUL!"
echo "You may now run 'cdk destroy' safely."
echo "=========================================="