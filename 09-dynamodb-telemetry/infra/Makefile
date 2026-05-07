# Check environment for ESP_NODE_ID; if not found, default to ESP_NODE_001
ESP_NODE_ID ?= ESP_NODE_001

# Export it so that 'cdk' and your shell scripts can read it
export ESP_NODE_ID

.PHONY: setup deploy destroy onboard-thing offboard-thing

setup:
	cdk bootstrap

deploy:
	@echo "================================================="
	@echo "Deploying infrastructure..."
	@echo "================================================="
	cdk deploy --all --require-approval never

destroy:
	@echo "================================================="
	@echo "Deploying infrastructure..."
	@echo "================================================="
	./scripts/cleanup.sh \
	&& cdk destroy --all --require-approval never


onboard-thing:
	@echo "================================================="
	@echo "Onboarding Node ID: $$ESP_NODE_ID"
	@echo "================================================="
	./scripts/setup_certs.sh

offboard-thing:
	@echo "================================================="
	@echo "Offboarding Node ID: $$ESP_NODE_ID"
	@echo "================================================="	
	./scripts/delete_thing.sh