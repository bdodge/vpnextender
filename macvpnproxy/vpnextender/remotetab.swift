//
//  remotetab.swift
//  vpnextender
//
//  Created by Brian Dodge on 1/31/21.
//

import Cocoa

class remotetab: NSViewController, NSTextFieldDelegate {
    @IBOutlet weak var remote_host: NSTextField!
    @IBOutlet weak var remote_port: NSTextField!
    
    var myParent:ViewController? = nil
    
    override func viewDidLoad() {
        super.viewDidLoad()
        myParent = parent! as? ViewController
        
        remote_host.delegate = self
        remote_port.delegate = self
    }
    
    override func viewWillAppear() {
        if (myParent != nil) {
           remote_host.stringValue = myParent!.def_remote_host
           remote_port.stringValue = String(myParent!.def_remote_port)
       }
    }

    override func viewWillDisappear() {
        super.viewWillDisappear()
    }
     
   func textField(textField: NSTextField, shouldChangeCharactersIn range: NSRange, replacementString string: String) -> Bool {
        let inverseSet = NSCharacterSet(charactersIn:"0123456789").inverted

        let components = string.components(separatedBy: inverseSet)

        let filtered = components.joined(separator: "")

        if filtered == string {
            return true
        } else {
            return false
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
        if let textField = obj.object as? NSTextField, self.remote_port.identifier == textField.identifier {
            let newtext = textField.stringValue;
            let cleantext = scrubPort(newtext)
            if (cleantext != newtext) {
                remote_port.stringValue = cleantext
            }
        }
    }

   @IBAction func onApply(_ sender: Any) {
        myParent?.def_remote_host = remote_host.stringValue
        if let portno = UInt16(remote_port.stringValue) {
            myParent?.def_remote_port = portno;
        }
        myParent?.StoreSettings()
    }
}

