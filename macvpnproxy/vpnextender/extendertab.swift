//
//  extendertab.swift
//  vpnextender
//
//  Created by Brian Dodge on 2/1/21.
//

import Cocoa

class extendertab: NSViewController {

    @IBOutlet weak var netname: NSTextField!
    @IBOutlet weak var netpass: NSTextField!
    
    var myParent:ViewController? = nil

    override func viewDidLoad() {
        super.viewDidLoad()
        myParent = parent! as? ViewController
        
    }
    
    override func viewWillAppear() {
        if (myParent != nil) {
            netname.stringValue = myParent!.def_netname
            netpass.stringValue = myParent!.def_netpass
       }
    }
   
    @IBAction func onApply(_ sender: Any) {
        myParent?.def_netname = netname.stringValue
        myParent?.def_netpass = netpass.stringValue
        myParent?.StoreSettings()
     }
}
