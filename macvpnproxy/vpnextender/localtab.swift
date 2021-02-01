//
//  localtab.swift
//  vpnextender
//
//  Created by Brian Dodge on 1/31/21.
//

import Cocoa

class localtab: NSViewController, NSTextFieldDelegate {

    @IBOutlet weak var local_port: NSTextField!
    
    var myParent:ViewController? = nil

    override func viewDidLoad() {
        super.viewDidLoad()
        myParent = parent! as? ViewController
        
        local_port.delegate = self
    }
    
    override func viewWillAppear() {
        if (myParent != nil) {
           local_port.stringValue = String(myParent!.def_local_port)
       }
    }
    
    func scrubPort(_ portstr: String) -> String {
        var retport:String = ""
        var i:Int
        var maxi:Int = portstr.count
        
        if (maxi > 5) {
            maxi = 5;
        }
        i = 0
        while (i < maxi) {
            let nc = portstr[portstr.index(portstr.startIndex, offsetBy: i)]
            if (nc >= "0" && nc <= "9") {
                retport += String(nc)
            }
            i += 1
        }
        if let numport = UInt16(retport) {
            retport = String(numport)
        }
        return retport
    }
    
    func controlTextDidChange(_ obj: Notification) {
        if let textField = obj.object as? NSTextField, self.local_port.identifier == textField.identifier {
            let newtext = textField.stringValue;
            let cleantext = scrubPort(newtext)
            if (cleantext != newtext) {
                local_port.stringValue = cleantext
            }
        }
    }

    @IBAction func onApply(_ sender: Any) {
        if let portno = UInt16(local_port.stringValue) {
            myParent?.def_local_port = portno;
        }
        myParent?.StoreSettings()
    }
}
