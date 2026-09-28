#!/bin/bash

case "$1" in
    rebuild)
        echo "Rebuilding driver and camera-sdk"
        make clean && make all
        ;;
    
    driver)
        echo "Building driver"
        make driver
        ;;
        
    camera-sdk)
        echo "Building camera-sdk"
        make camera-sdk
        ;;
        
    build)
        echo "Building all"
        make all
        ;;
        
    run)
        echo "Running sprint1 and homeing stepper"
        
        sudo -v 
    
        sudo ./build/driver > /dev/null 2>&1 &
        BG_PID=$!
        sleep 5
        sudo ./camera-sdk/build/bin/sweep_run
        
        echo "-1" | sudo tee -a /run/sprint1/data_pipe 
        echo "Exiting"
        wait $BG_PID
        ;;
        
    *)
        echo "Usage: $0 {rebuild|driver|camera-sdk|build|run}"
        exit 1
        ;;
esac
