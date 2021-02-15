//
//  ViewController.swift
//  vpnextender
//
//  Created by Brian Dodge on 1/24/21.
//

import Cocoa

class ViewController: NSTabViewController {

    var def_remote_port : [UInt16]  = [ 631, 0, 0, 0 ]
    var def_remote_host : [String]  = [ "", "", "", "" ]
    var def_local_port : [UInt16]   = [ 6631, 0, 0, 0 ]
    var def_vid : UInt16            = 0x3f0
    var def_pid : UInt16            = 0x102
    var def_log_level : UInt32      = 1
    var def_netname : String        = ""
    var def_netpass : String        = ""
    
    var settings : UserDefaults = UserDefaults.standard
    
    var xferq : DispatchQueue = DispatchQueue(label: "com.bdd.vpnxq", attributes: .concurrent)
    var xfer_work_item : DispatchWorkItem?
    var xferrunning : Bool = false
    
    override func loadView() {

        RestoreSettings()
        super.loadView()
    }
    
    override func viewDidLoad() {

        super.viewDidLoad()
        
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
        var hostlist : String = ""
        var rports : [UInt16] = Array(repeating: 0, count: Int(VPNX_MAX_PORTS))
        var lports : [UInt16] = Array(repeating: 0, count: Int(VPNX_MAX_PORTS))
               
        var i = 0;
        
        repeat {
            hostlist = hostlist + def_remote_host[i];
            if (i < 3 && def_remote_host[i + 1] != "") {
                hostlist += ","
            }
            rports[i] = def_remote_port[i]
            lports[i] = def_local_port[i]
            i = i + 1
        }
        while (i < 4 && i < VPNX_MAX_PORTS)
        
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

    func RestoreSettings() {
        if settings.string(forKey: "Remote Host_0") != nil {
            // remote host is defined in settings, use settings as our starting
            // point for the settings
            //
            var i = 0;
            
            repeat {
                let keymod = "_" + String(i)
                if let remotehost = settings.string(forKey: "Remote Host" + keymod) {
                    def_remote_host[i] = remotehost
                }
                else {
                    def_remote_host[i] = ""
                }
                def_remote_port[i] = (UInt16)(settings.integer(forKey: "Remote Port" + keymod))
                def_local_port[i]  = (UInt16)(settings.integer(forKey: "Local Port" + keymod))
                i = i + 1
            }
            while (i < 4)
            
            def_vid         = (UInt16)(settings.integer(forKey: "VendorID"))
            def_pid         = (UInt16)(settings.integer(forKey: "ProductID"))
            def_log_level   = (UInt32)(settings.integer(forKey: "Log Level"))
            def_netname     = settings.string(forKey: "Network") ?? ""
            def_netpass     = settings.string(forKey: "Password") ?? ""
        }
        else {
            // no such setting so make them up and write them out
            //
            StoreSettings()
        }
    }
    
    func StoreSettings() {
        var i = 0
        
        repeat {
            let keymod = "_" + String(i)
            
            settings.set(def_remote_host[i], forKey: "Remote Host" + keymod)
            settings.set(def_remote_port[i], forKey: "Remote Port" + keymod)
            settings.set(def_local_port[i], forKey: "Local Port" + keymod)
            i = i + 1
        }
        while (i < 4)
        
        settings.set(def_vid, forKey: "VendorID")
        settings.set(def_pid, forKey: "ProductID")
        settings.set(def_log_level, forKey: "Log Level")
        settings.set(def_netname, forKey: "Network")
        settings.set(def_netpass, forKey: "Password")
    }

    func DefaultSettings() {
        def_remote_port[0] = 631
        def_remote_host[0] = ""
        def_local_port[0]  = 6631
        
        var i = 0
        
        repeat {
            def_remote_host[i] = "";
            def_remote_port[i] = 0;
            def_local_port[i] = 0;
            i = i + 1
        } while (i < 4)
        
        def_vid            = 0x3f0
        def_pid            = 0x102
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

