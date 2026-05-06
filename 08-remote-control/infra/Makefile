.PHONY: setup deploy destroy

setup:
	cdk bootstrap

deploy:
	cdk deploy --all --require-approval never \
	&& ./scripts/setup_certs.sh

destroy:
	./scripts/cleanup.sh \
	&& cdk destroy --all --require-approval never