//
//  remotetab.swift
//  vpnextender
//
//  Created by Brian Dodge on 1/31/21.
//

import Cocoa

class remotetab: NSViewController, NSTextFieldDelegate {
    @IBOutlet weak var lport1: NSTextField!
    @IBOutlet weak var lport2: NSTextField!
    @IBOutlet weak var lport3: NSTextField!
    @IBOutlet weak var lport4: NSTextField!
    @IBOutlet weak var rhost1: NSTextField!
    @IBOutlet weak var rhost2: NSTextField!
    @IBOutlet weak var rhost3: NSTextField!
    @IBOutlet weak var rhost4: NSTextField!
    @IBOutlet weak var rport1: NSTextField!
    @IBOutlet weak var rport2: NSTextField!
    @IBOutlet weak var rport3: NSTextField!
    @IBOutlet weak var rport4: NSTextField!
    
    var myParent:ViewController? = nil
    
    override func viewDidLoad() {
        super.viewDidLoad()
        myParent = parent! as? ViewController
        
        lport1.delegate = self
        rport1.delegate = self
        rhost1.delegate = self
        lport2.delegate = self
        rport2.delegate = self
        rhost2.delegate = self
        lport3.delegate = self
        rport3.delegate = self
        rhost3.delegate = self
        lport4.delegate = self
        rport4.delegate = self
        rhost4.delegate = self
    }
    
    override func viewWillAppear() {
        if (myParent != nil) {
            rhost1.stringValue = myParent!.def_remote_host[0]
            lport1.stringValue = String(myParent!.def_local_port[0])
            rport1.stringValue = String(myParent!.def_remote_port[0])
            rhost2.stringValue = myParent!.def_remote_host[1]
            lport2.stringValue = String(myParent!.def_local_port[1])
            rport2.stringValue = String(myParent!.def_remote_port[1])
            rhost3.stringValue = myParent!.def_remote_host[2]
            lport3.stringValue = String(myParent!.def_local_port[2])
            rport3.stringValue = String(myParent!.def_remote_port[2])
            rhost4.stringValue = myParent!.def_remote_host[3]
            lport4.stringValue = String(myParent!.def_local_port[3])
            rport4.stringValue = String(myParent!.def_remote_port[3])
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
        if let textField = obj.object as? NSTextField, self.rport1.identifier == textField.identifier {
            let newtext = textField.stringValue;
            let cleantext = scrubPort(newtext)
            if (cleantext != newtext) {
                rport1.stringValue = cleantext
            }
        }
        else if let textField = obj.object as? NSTextField, self.rport2.identifier == textField.identifier {
            let newtext = textField.stringValue;
            let cleantext = scrubPort(newtext)
            if (cleantext != newtext) {
                rport2.stringValue = cleantext
            }
        }
        else if let textField = obj.object as? NSTextField, self.rport3.identifier == textField.identifier {
            let newtext = textField.stringValue;
            let cleantext = scrubPort(newtext)
            if (cleantext != newtext) {
                rport3.stringValue = cleantext
            }
        }
        else if let textField = obj.object as? NSTextField, self.rport4.identifier == textField.identifier {
            let newtext = textField.stringValue;
            let cleantext = scrubPort(newtext)
            if (cleantext != newtext) {
                rport4.stringValue = cleantext
            }
        }
        
        if let textField = obj.object as? NSTextField, self.lport1.identifier == textField.identifier {
            let newtext = textField.stringValue;
            let cleantext = scrubPort(newtext)
            if (cleantext != newtext) {
                lport1.stringValue = cleantext
            }
        }
        else if let textField = obj.object as? NSTextField, self.lport2.identifier == textField.identifier {
            let newtext = textField.stringValue;
            let cleantext = scrubPort(newtext)
            if (cleantext != newtext) {
                lport2.stringValue = cleantext
            }
        }
        else if let textField = obj.object as? NSTextField, self.lport3.identifier == textField.identifier {
            let newtext = textField.stringValue;
            let cleantext = scrubPort(newtext)
            if (cleantext != newtext) {
                lport3.stringValue = cleantext
            }
        }
        else if let textField = obj.object as? NSTextField, self.lport4.identifier == textField.identifier {
            let newtext = textField.stringValue;
            let cleantext = scrubPort(newtext)
            if (cleantext != newtext) {
                lport4.stringValue = cleantext
            }
        }
    }

   @IBAction func onApply(_ sender: Any) {
        myParent?.def_remote_host[0] = rhost1.stringValue
        if let portno = UInt16(rport1.stringValue) {
            myParent?.def_remote_port[0] = portno;
        }
        if let portno = UInt16(lport1.stringValue) {
            myParent?.def_local_port[0] = portno;
        }
    
        myParent?.def_remote_host[1] = rhost2.stringValue
        if let portno = UInt16(rport2.stringValue) {
            myParent?.def_remote_port[1] = portno;
        }
        if let portno = UInt16(lport2.stringValue) {
            myParent?.def_local_port[1] = portno;
        }

        myParent?.def_remote_host[2] = rhost3.stringValue
        if let portno = UInt16(rport3.stringValue) {
            myParent?.def_remote_port[2] = portno;
        }
        if let portno = UInt16(lport3.stringValue) {
            myParent?.def_local_port[2] = portno;
        }

        myParent?.def_remote_host[3] = rhost4.stringValue
        if let portno = UInt16(rport4.stringValue) {
            myParent?.def_remote_port[3] = portno;
        }
        if let portno = UInt16(lport4.stringValue) {
            myParent?.def_local_port[3] = portno;
        }

        myParent?.StoreSettings()
    }
}

