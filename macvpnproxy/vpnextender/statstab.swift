//
//  statstab.swift
//  vpnextender
//
//  Created by Brian Dodge on 1/31/21.
//

import Cocoa

class statstab: NSViewController {

    @IBOutlet weak var local_status: NSTextField!
    @IBOutlet weak var remote_status: NSTextField!
    @IBOutlet weak var tx_count: NSTextField!
    @IBOutlet weak var rx_count: NSTextField!
    @IBOutlet weak var logtext: NSScrollView!
    @IBOutlet weak var loglevel: NSComboBox!
    
    var myParent:ViewController? = nil
    var logtimer:Timer? = nil
    
    override func viewDidLoad() {
        super.viewDidLoad()
        myParent = parent! as? ViewController
        
        // Do view setup here.
        tx_count.stringValue = "0"
        rx_count.stringValue = "0"
        local_status.stringValue = ""
        remote_status.stringValue = ""
        
        var loglev: Int
        
        if let level = myParent?.def_log_level {
            loglev = Int(level)
        }
        else {
            loglev = 1
        }
        loglevel.selectItem(at: loglev)
        
        // start a timer for readinf the mem log (use objc code to allow earlier target sdk)
        logtimer = Timer.scheduledTimer(timeInterval: 0.25, target: self,
                            selector: #selector(onTimer), userInfo: nil, repeats: true)
    }
    
    override func viewWillDisappear() {
        super.viewWillDisappear()
        logtimer?.invalidate()
        logtimer = nil
        myParent?.StoreSettings()
    }
        
    @objc func onTimer()
    {
        let bytes = UnsafeMutablePointer<Int8>.allocate(capacity: 256)
        
        repeat {
            vpnx_get_log_string(bytes, 256)
            if (bytes[0] != 0) {
                addToLog(String.init(cString: bytes))
            }
        }
        while (bytes[0] != 0)
        
        var xfer = vpnx_xfer_tx_count()
        tx_count.stringValue = String(xfer)

        xfer = vpnx_xfer_rx_count()
        rx_count.stringValue = String(xfer)
        
        local_status.stringValue = String(cString: vpnx_local_status())
        remote_status.stringValue = String(cString: vpnx_extender_status())
    }
    
    func addToLog(_ msg: String) {
        logtext.documentView!.insertText(msg)
    }
    
    @IBAction func onClear(_ sender: Any) {
        logtext.documentView!.selectAll(Any?.self)
        logtext.documentView!.insertText("")
    }
    
    @IBAction func onLogLevel(_ sender: Any) {
        myParent?.def_log_level = UInt32(loglevel.indexOfSelectedItem)
        myParent?.StoreSettings()
        vpnx_set_log_level(UInt32(loglevel.indexOfSelectedItem))
    }
}
