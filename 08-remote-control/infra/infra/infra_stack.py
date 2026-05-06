from aws_cdk import (
    Stack,
    aws_iot as iot,
    CfnOutput
)
from constructs import Construct

class InfraStack(Stack):

    def __init__(self, scope: Construct, construct_id: str, **kwargs) -> None:
        super().__init__(scope, construct_id, **kwargs)

        # Step 1: We need to create a security policy
        policy_document = {
            "Version": "2012-10-17",
            "Statement": [
                {
                    "Effect": "Allow",
                    "Action": "iot:*",
                    "Resource": "*"
                }
            ]
        }

        # We tell AWS to create the policy using that dictionary
        esp_policy = iot.CfnPolicy(
            self,
            "EspCDKPolicy",
            policy_name="ESP_Dev_Policy_CDK",
            policy_document=policy_document
        )


        # Step 2: We need to create a "Thing" (device) in AWS IoT Core
        # This will act as a digital twin to our ESP Chip
        esp_thing = iot.CfnThing(
            self,
            "EspCDKThing",
            thing_name="ESP8684_Node_CDK"
        )

        # Step 3: Outputs
        # When the deployment finishes, print these values to the terminal
        CfnOutput(self, "ThingCreated", value=esp_thing.thing_name)
        CfnOutput(self, "PolicyCreated", value=esp_policy.policy_name)


