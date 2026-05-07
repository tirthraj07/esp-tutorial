#!/bin/bash

# Default to the fleet policy name we defined in CDK, 
# but allow the user to override it via an argument if needed.
POLICY_NAME="${1:-ESP_Fleet_Policy}"

echo "=========================================================="
echo "Preparing to isolate Policy: $POLICY_NAME"
echo "=========================================================="

echo "1. Scanning for attached targets (Certificates/Principals)..."

# Fetch every single target attached to this policy
TARGETS=$(aws iot list-policy-targets \
    --policy-name "$POLICY_NAME" \
    --query 'targets[*]' \
    --output text 2>/dev/null)

if [ -z "$TARGETS" ] || [ "$TARGETS" == "None" ]; then
    echo "-> No certificates attached. The policy is already isolated!"
    echo "-> You are safe to run 'cdk destroy'."
    exit 0
fi

# Convert the space/tab separated output into an array we can loop over
TARGET_ARRAY=($TARGETS)
TARGET_COUNT=${#TARGET_ARRAY[@]}

echo "-> Found $TARGET_COUNT target(s) attached."

echo "2. Detaching targets..."
for TARGET in "${TARGET_ARRAY[@]}"; do
    echo "   - Detaching: $TARGET"
    aws iot detach-policy \
        --policy-name "$POLICY_NAME" \
        --target "$TARGET"
done

echo "=========================================================="
echo "SUCCESS! All certificates have been severed."
echo "Policy '$POLICY_NAME' is now completely isolated."
echo "You may now safely run 'cdk destroy' to delete the cloud infrastructure."
echo "=========================================================="