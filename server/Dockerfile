FROM golang:1.27.4-alpine AS build
WORKDIR /src
COPY go.mod go.sum ./
RUN go mod download
COPY *.go ./
RUN go test ./... && CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o /voice .

FROM scratch
COPY --from=build /voice /voice
USER 65534:65534
EXPOSE 8090
ENTRYPOINT ["/voice"]
