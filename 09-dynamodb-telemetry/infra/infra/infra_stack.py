from aws_cdk import (
    Stack,
    aws_iot as iot,
    aws_dynamodb as dynamodb,
    aws_iam as iam,
    RemovalPolicy,
    CfnOutput
)
from constructs import Construct

class InfraStack(Stack):

    def __init__(self, scope: Construct, construct_id: str, **kwargs) -> None:
        super().__init__(scope, construct_id, **kwargs)
        # =======================================================
        # THE SECURITY LAYER
        # =======================================================
        # 1. The Fleet-Wide Security Policy
        policy_document = {
            "Version": "2012-10-17",
            "Statement": [
                {
                    "Effect": "Allow",
                    "Action": "iot:Connect",
                    # The chip can only connect if its MQTT Client ID matches its Node ID
                    "Resource": f"arn:aws:iot:*:*:client/${{iot:Connection.Thing.ThingName}}" 
                },
                {
                    "Effect": "Allow",
                    "Action": "iot:Publish",
                    # It can only publish to: nodes/{NodeID}/*
                    "Resource": f"arn:aws:iot:*:*:topic/nodes/${{iot:Connection.Thing.ThingName}}/*"
                },
                {
                    "Effect": "Allow",
                    "Action": "iot:Subscribe",
                    # It can only subscribe to: nodes/{NodeID}/*
                    "Resource": f"arn:aws:iot:*:*:topicfilter/nodes/${{iot:Connection.Thing.ThingName}}/*"
                },
                {
                    "Effect": "Allow",
                    "Action": "iot:Receive",
                    "Resource": f"arn:aws:iot:*:*:topic/nodes/${{iot:Connection.Thing.ThingName}}/*"
                }
            ]
        }

        # This one policy rules them all.
        esp_policy = iot.CfnPolicy(
            self,
            "EspCDKPolicy",
            policy_name="ESP_Fleet_Policy",
            policy_document=policy_document
        )

        # =======================================================
        # THE DATABASE LAYER
        # =======================================================

        # Create dynamodb table
        telemetry_table = dynamodb.Table(
            self,
            "ESP_Telemetry_Data",
            table_name="ESP_Telemetry_Data",
            # Partition Key groups data by the specific device
            partition_key=dynamodb.Attribute(name="node_id", type=dynamodb.AttributeType.STRING),
            # Sort Key organizes that device's data chronologically
            sort_key=dynamodb.Attribute(name="timestamp", type=dynamodb.AttributeType.NUMBER),
            billing_mode=dynamodb.BillingMode.PAY_PER_REQUEST,
            removal_policy=RemovalPolicy.DESTROY
        )

        # =======================================================
        # THE ROUTING LAYER
        # =======================================================
        # Create permissions allowing the IoT Core to write to the Database
        iot_role = iam.Role(
            self,
            "IoTRuleRole",
            assumed_by=iam.ServicePrincipal("iot.amazonaws.com")
        )

        telemetry_table.grant_write_data(iot_role)

        # Write the SQL statement.
        # Note the 'timestamp() AS timestamp' -> This injects the exact cloud ingestion time!
        sql_statement = "SELECT *, timestamp() AS timestamp FROM 'nodes/+/telemetry'"

        # Create the IoT SQL Rule
        iot_rule = iot.CfnTopicRule(
            self,
            "TelemetryRoutingRule",
            rule_name="Route_ESP_Telemetry_To_Dynamo",
            topic_rule_payload=iot.CfnTopicRule.TopicRulePayloadProperty(
                sql=sql_statement,
                actions=[
                    iot.CfnTopicRule.ActionProperty(
                        # We use DynamoDBv2 to automatically map JSON keys to DB columns
                        dynamo_d_bv2=iot.CfnTopicRule.DynamoDBv2ActionProperty(
                            put_item=iot.CfnTopicRule.PutItemInputProperty(
                                table_name=telemetry_table.table_name
                            ),
                            role_arn=iot_role.role_arn
                        )
                    )
                ]
            )
        )


        # Outputs
        # When the deployment finishes, print these values to the terminal
        CfnOutput(self, "FleetPolicyName", value=esp_policy.policy_name)
        CfnOutput(self, "DynamoTableName", value=telemetry_table.table_name)
        CfnOutput(self, "IoTRuleName", value=iot_rule.rule_name)

