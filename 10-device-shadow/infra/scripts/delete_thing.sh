#!/bin/bash

ESP_NODE_ID="${ESP_NODE_ID:-ESP_NODE_001}"
THING_NAME="${ESP_NODE_ID}"
POLICY_NAME="ESP_Fleet_Policy"

echo "=========================================="
echo "Cleaning up Device: $THING_NAME"
echo "=========================================="

echo "1. Finding the attached Certificate..."
CERT_ARN=$(aws iot list-thing-principals --thing-name $THING_NAME --query 'principals[0]' --output text 2>/dev/null)

if [ "$CERT_ARN" != "None" ] && [ -n "$CERT_ARN" ]; then
    CERT_ID=$(echo $CERT_ARN | cut -d'/' -f2)
    echo "Found Certificate ID: $CERT_ID"

    echo "2. Detaching Certificate from Thing..."
    aws iot detach-thing-principal --thing-name $THING_NAME --principal $CERT_ARN

    echo "3. Detaching Fleet Policy from Certificate..."
    aws iot detach-policy --policy-name $POLICY_NAME --target $CERT_ARN

    echo "4. Deactivating Certificate..."
    aws iot update-certificate --certificate-id $CERT_ID --new-status INACTIVE

    echo "5. Deleting Certificate..."
    aws iot delete-certificate --certificate-id $CERT_ID
else
    echo "No certificates found attached to $THING_NAME."
fi

echo "6. Deleting Thing from AWS IoT Core..."
# We must delete the Thing itself now, since CDK no longer manages it!
aws iot delete-thing --thing-name $THING_NAME

echo "=========================================="
echo "CLEANUP SUCCESSFUL for $THING_NAME."
echo "=========================================="