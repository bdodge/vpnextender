//
//  ViewController.swift
//  vpnextender
//
//  Created by Brian Dodge on 1/24/21.
//

import Cocoa

class ViewController: NSTabViewController {

    var def_remote_port : UInt16    = 631
    var def_remote_host : String    = ""
    var def_vid : UInt16            = 0x3f0
    var def_pid : UInt16            = 0x102
    var def_local_port : UInt16     = 6631
    var def_log_level : UInt32      = 1
    var def_netname : String        = ""
    var def_netpass : String        = ""
    
    var settings : UserDefaults = UserDefaults.standard
    
    var xferq : DispatchQueue = DispatchQueue(label: "com.bdd.vpnxq", attributes: .concurrent)
    var xfer_work_item : DispatchWorkItem?
    var xferrunning : Bool = false
    
    override func viewDidLoad() {

        super.viewDidLoad()
        
        if let remotehost = settings.string(forKey: "Remote Host") {
            // remote host is defined in settings, use settings as our starting
            // point for the settings
            //
            def_remote_host = remotehost;
            def_remote_port = (UInt16)(settings.integer(forKey: "Remote Port"))
            def_vid         = (UInt16)(settings.integer(forKey: "VendorID"))
            def_pid         = (UInt16)(settings.integer(forKey: "ProductID"))
            def_local_port  = (UInt16)(settings.integer(forKey: "Local Port"))
            def_log_level   = (UInt32)(settings.integer(forKey: "Log Level"))
            def_netname     = settings.string(forKey: "Network") ?? ""
            def_netpass     = settings.string(forKey: "Password") ?? ""
        }
        else {
            // no such setting so make them up and write them out
            //
            StoreSettings()
        }
        StartExtender()
    }
    
    override var representedObject: Any? {
        didSet {
        // Update the view, if already loaded.
        }
    }

    func StartExtender() {
        var result : Int32
        
        if (xferrunning) {
            print("Already running xfer")
            return
        }
        var hostlist : String = def_remote_host
        var rports : [UInt16] = Array(repeating: 0, count: Int(VPNX_MAX_PORTS))
        var lports : [UInt16] = Array(repeating: 0, count: Int(VPNX_MAX_PORTS))
               
        rports[0] = def_remote_port
        lports[0] = def_local_port
        
        // Initialize the prtproxy
        //
        result = vpnx_gui_init(
                    true,
                    hostlist,
                    rports,
                    def_vid,
                    def_pid,
                    lports,
                    def_log_level,
                    vpnx_mem_logger
                    );

        if (result == 0) {
            // run its loop in our queue
            //
            xfer_work_item = DispatchWorkItem { self.RunXfer() }
            xferq.async(execute: xfer_work_item!)
        }
        else {
            print("Failed to start extender"); // TODO announce failure?
        }
    }
    
    func StopExtender() {
        print("cancelling xfer")
        xfer_work_item!.cancel();
    }
    
    func XferStopped() {
        print("xfer stopped, restarting")
        StartExtender();
    }
    
    func StoreSettings() {
        settings.set(def_remote_host, forKey: "Remote Host")
        settings.set(def_remote_port, forKey: "Remote Port")
        settings.set(def_vid, forKey: "VendorID")
        settings.set(def_pid, forKey: "ProductID")
        settings.set(def_local_port, forKey: "Local Port")
        settings.set(def_log_level, forKey: "Log Level")
        settings.set(def_netname, forKey: "Network")
        settings.set(def_netpass, forKey: "Password")
    }

    func DefaultSettings() {
        def_remote_port    = 631
        def_remote_host    = ""
        def_vid            = 0x3f0
        def_pid            = 0x102
        def_local_port     = 6631
        def_log_level      = 1
        def_netname        = ""
        def_netpass        = ""
    }
    
    func RunXfer() {
        var result : Int32
        
        self.xferrunning = true
        
        repeat {
            result = vpnx_gui_slice()
        }
        while (result == 0 && !xfer_work_item!.isCancelled)
        
        print("RunXfer ends")
        
        self.xferrunning = false

        xfer_work_item?.notify(queue: .main) {
            self.XferStopped()
        }
    }
}

