FROM nixos/nix:latest

RUN echo "experimental-features = nix-command flakes" >> /etc/nix/nix.conf

WORKDIR /code

COPY flake.nix flake.lock ./

RUN nix develop --command true

ENTRYPOINT ["nix", "develop"]
