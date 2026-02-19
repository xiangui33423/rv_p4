#!/bin/bash
# Docker setup and run script for RV-P4 ASIC flow

set -e

DOCKER_IMAGE="rv_p4_asic:latest"
DOCKER_CONTAINER="rv_p4_asic_work"
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")"; pwd)"

# ============================================================
# Functions
# ============================================================

log() { echo "[$(date +%H:%M:%S)] $1"; }
error() { log "✗ $1"; exit 1; }
success() { log "✓ $1"; }

check_docker() {
    if ! command -v docker &> /dev/null; then
        error "Docker not found. Install from https://docs.docker.com/get-docker/"
    fi
    success "Docker found: $(docker --version)"
}

# ============================================================
# Main
# ============================================================

main() {
    log "=========================================="
    log "RV-P4 ASIC Design Flow - Docker Setup"
    log "=========================================="

    check_docker

    # Build image
    log "Building Docker image (this takes 20-40 minutes)..."
    log "Please be patient..."

    docker build -t "${DOCKER_IMAGE}" -f "${PROJECT_DIR}/Dockerfile" "${PROJECT_DIR}" 2>&1 | \
        tail -20

    if [ $? -eq 0 ]; then
        success "Docker image built: ${DOCKER_IMAGE}"
    else
        error "Docker build failed"
    fi

    # Create container
    log ""
    log "Creating Docker container..."
    docker run -d \
        --name "${DOCKER_CONTAINER}" \
        -v "${PROJECT_DIR}:/work" \
        -w /work \
        "${DOCKER_IMAGE}" \
        sleep infinity > /dev/null 2>&1 || true

    success "Container ready"

    # Show usage
    log ""
    log "=========================================="
    log "Ready to run design flow!"
    log "=========================================="
    log ""
    log "Run commands inside Docker:"
    log ""
    log "  docker exec ${DOCKER_CONTAINER} make -f Makefile.asic synth"
    log "  docker exec ${DOCKER_CONTAINER} make -f Makefile.asic place"
    log "  docker exec ${DOCKER_CONTAINER} make -f Makefile.asic route"
    log "  docker exec ${DOCKER_CONTAINER} make -f Makefile.asic all"
    log ""
    log "Or start interactive shell:"
    log "  docker exec -it ${DOCKER_CONTAINER} bash"
    log ""
    log "Clean up:"
    log "  docker stop ${DOCKER_CONTAINER}"
    log "  docker rm ${DOCKER_CONTAINER}"
    log "  docker rmi ${DOCKER_IMAGE}"
}

main
